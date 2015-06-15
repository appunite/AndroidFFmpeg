/*
 * player.c
 * Copyright (c) 2012 Jacek Marchwicki
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#include <libavutil/avstring.h>
#include <libavutil/pixdesc.h>
#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>

#include <libswscale/swscale.h>

//#include <libavcodec/opt.h>
#include <libavcodec/avfft.h>

#include <android/bitmap.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>

#include <jni.h>
#include <pthread.h>

/* Android profiler */
#ifdef PROFILER
#include <android-ndk-profiler-3.1/prof.h>
#endif

/*local headers*/

#include "convert.h"
#include "helpers.h"
#include "queue.h"
#include "player.h"
#include "jni-protocol.h"
#include "aes-protocol.h"
#include "sync.h"

#define FFMPEG_LOG_LEVEL AV_LOG_WARNING
#define LOG_LEVEL 2
#define LOG_TAG "player.c"
#define LOGI(level, ...) if (level <= LOG_LEVEL) {__android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__);}
#define LOGE(level, ...) if (level <= LOG_LEVEL + 10) {__android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__);}
#define LOGW(level, ...) if (level <= LOG_LEVEL + 5) {__android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__);}

#define FALSE 0
#define TRUE (!(FALSE))

#define SUBTITLES

#ifdef SUBTITLES
#include "blend.h"
#include <ass/ass.h>
#endif // SUBTITLES
#define DO_NOT_SEEK -1

// 1000000 us = 1 s
#define MIN_SLEEP_TIME_US 10000
// 10000 ms = 1s
#define MIN_SLEEP_TIME_MS 2

//#define MEASURE_TIME

#ifdef MEASURE_TIME
struct timespec render_frame_start, render_frame_stop;

// http://www.guyrutenberg.com/2007/09/22/profiling-code-using-clock_gettime/
struct timespec timespec_diff(struct timespec start, struct timespec end) {
	struct timespec temp;
	if ((end.tv_nsec - start.tv_nsec) < 0) {
		temp.tv_sec = end.tv_sec - start.tv_sec - 1;
		temp.tv_nsec = 1000000000 + end.tv_nsec - start.tv_nsec;
	} else {
		temp.tv_sec = end.tv_sec - start.tv_sec;
		temp.tv_nsec = end.tv_nsec - start.tv_nsec;
	}
	return temp;
}
#endif // MEASURE_TIME
void player_print_codec_description(const struct AVCodec *codec) {
	char *type = "???";
	switch (codec->type) {
	case AVMEDIA_TYPE_ATTACHMENT:
		type = "attachment";
		break;

	case AVMEDIA_TYPE_AUDIO:
		type = "audio";
		break;
	case AVMEDIA_TYPE_DATA:
		type = "data";
		break;
	case AVMEDIA_TYPE_NB:
		type = "nb";
		break;
	case AVMEDIA_TYPE_SUBTITLE:
		type = "subtitle";
		break;
	case AVMEDIA_TYPE_UNKNOWN:
		type = "unknown";
		break;
	case AVMEDIA_TYPE_VIDEO:
		type = "video";
		break;
	default:
		break;
	}
	LOGI(10,
			"player_print_codec_description id: %d codec: %s, type: %s",
			codec->id, codec->name, type);
}

struct SubtitleElem {
	AVSubtitle subtitle;
	int64_t start_time;
	int64_t stop_time;
};

struct PacketData {
	int end_of_stream;
	AVPacket *packet;
};

struct DecoderState {
	int stream_no;
	struct Player *player;
	JNIEnv* env;
};

struct DecoderData {
	struct Player *player;
	int stream_no;
};

#define MAX_STREAMS 3

struct Player {
	JavaVM *get_javavm;
	jobject thiz;

	ANativeWindow* window;
	AVFrame *rgb_frame;

	AVFrame *tmp_frame;
	uint8_t *tmp_buffer;
	AVFrame *tmp_frame2;
	uint8_t *tmp_buffer2;

	int64_t video_duration;
	int64_t last_updated_time;

	jclass player_class;
	jclass audio_track_class;
	jmethodID audio_track_write_method;
	jmethodID audio_track_play_method;
	jmethodID audio_track_pause_method;
	jmethodID audio_track_flush_method;
	jmethodID audio_track_stop_method;
	jmethodID audio_track_get_channel_count_method;
	jmethodID audio_track_get_sample_rate_method;

	jmethodID player_prepare_frame_method;
	jmethodID player_on_update_time_method;
	jmethodID player_prepare_audio_track_method;
	jmethodID player_set_stream_info_method;

	pthread_mutex_t mutex_operation;

	int caputre_streams_no;

	int video_stream_no;
	int audio_stream_no;
	AVStream *input_streams[MAX_STREAMS];
	AVCodecContext * input_codec_ctxs[MAX_STREAMS];
	int input_stream_numbers[MAX_STREAMS];
	AVFrame *input_frames[MAX_STREAMS];

	AVFormatContext *input_format_ctx;
	int input_inited;

	jobject audio_track;
	enum AVSampleFormat audio_track_format;
	int audio_track_channel_count;

	struct SwsContext *sws_context;

	struct SwrContext *swr_context;
	DECLARE_ALIGNED(16,uint8_t,audio_buf2)[AVCODEC_MAX_AUDIO_FRAME_SIZE * 4];

	int playing;

	pthread_mutex_t mutex_queue;
	pthread_cond_t cond_queue;
	Queue *packets[MAX_STREAMS];
	Queue *subtitles_queue;

	int pause;
	int stop;
	int64_t seek_position;
	int flush_streams[MAX_STREAMS];
	int flush_video_play;

	int stop_streams[MAX_STREAMS];

	pthread_t thread_player_read_from_stream;
	pthread_t decode_threads[MAX_STREAMS];

	int thread_player_read_from_stream_created;
	int decode_threads_created[MAX_STREAMS];

	int64_t audio_clock;

	int64_t start_time;
	int64_t pause_time;

#ifdef SUBTITLES
	int subtitle_stream_no;
	ASS_Library * ass_library;
	ASS_Renderer * ass_renderer;
	ASS_Track * ass_track;
	pthread_mutex_t mutex_ass;
#endif // SUBTITLES
};

struct State {
	struct Player *player;
	JNIEnv* env;
};

enum Msgs {
	MSG_NONE = 0, MSG_STOP = 1
};

enum PlayerErrors {
	ERROR_NO_ERROR = 0,

	// Java errors
	ERROR_COULD_NOT_CREATE_GLOBAL_REF,
	ERROR_NOT_FOUND_PLAYER_CLASS,
	ERROR_NOT_FOUND_PREPARE_FRAME_METHOD,
	ERROR_NOT_FOUND_ON_UPDATE_TIME_METHOD,
	ERROR_NOT_FOUND_PREPARE_AUDIO_TRACK_METHOD,
	ERROR_NOT_FOUND_SET_STREAM_INFO_METHOD,
	ERROR_NOT_FOUND_M_NATIVE_PLAYER_FIELD,
	ERROR_COULD_NOT_GET_JAVA_VM,
	ERROR_COULD_NOT_DETACH_THREAD,
	ERROR_COULD_NOT_ATTACH_THREAD,
	ERROR_COULD_NOT_CREATE_GLOBAL_REF_FOR_AUDIO_TRACK_CLASS,

	// AudioTrack
	ERROR_NOT_FOUND_AUDIO_TRACK_CLASS,
	ERROR_NOT_FOUND_WRITE_METHOD,
	ERROR_NOT_FOUND_PLAY_METHOD,
	ERROR_NOT_FOUND_PAUSE_METHOD,
	ERROR_NOT_FOUND_STOP_METHOD,
	ERROR_NOT_FOUND_GET_CHANNEL_COUNT_METHOD,
	ERROR_NOT_FOUND_FLUSH_METHOD,
	ERROR_NOT_FOUND_GET_SAMPLE_RATE_METHOD,

	ERROR_COULD_NOT_CREATE_AVCONTEXT,
	ERROR_COULD_NOT_OPEN_VIDEO_FILE,
	ERROR_COULD_NOT_OPEN_STREAM,
	ERROR_COULD_NOT_OPEN_VIDEO_STREAM,
	ERROR_COULD_NOT_FIND_VIDEO_CODEC,
	ERROR_COULD_NOT_OPEN_VIDEO_CODEC,
	ERROR_COULD_NOT_ALLOC_FRAME,

	ERROR_NOT_CREATED_BITMAP,
	ERROR_COULD_NOT_GET_SWS_CONTEXT,
	ERROR_COULD_NOT_PREPARE_PACKETS_QUEUE,
	ERROR_COULD_NOT_FIND_AUDIO_STREAM,
	ERROR_COULD_NOT_FIND_AUDIO_CODEC,
	ERROR_COULD_NOT_OPEN_AUDIO_CODEC,
	ERROR_COULD_NOT_PREPARE_RGB_QUEUE,
#ifdef SUBTITLES
	ERROR_COULD_NOT_PREPARE_SUBTITLES_QUEUE,
	ERROR_COULD_NOT_INIT_ASS_LIBRARY,
	ERROR_COULD_NOT_PREAPARE_ASS_TRACK,
	ERROR_COULD_NOT_PREPARE_ASS_RENDERER,
#endif // SUBTITLES
	ERROR_COULD_NOT_PREPARE_AUDIO_PACKETS_QUEUE,
	ERROR_COULD_NOT_PREPARE_VIDEO_PACKETS_QUEUE,

	ERROR_WHILE_DUPLICATING_FRAME,

	ERROR_WHILE_DECODING_VIDEO,
	ERROR_COULD_NOT_RESAMPLE_FRAME,
	ERROR_WHILE_ALLOCATING_AUDIO_SAMPLE,
	ERROR_WHILE_DECODING_AUDIO_FRAME,
	ERROR_NOT_CREATED_AUDIO_TRACK,
	ERROR_NOT_CREATED_AUDIO_TRACK_GLOBAL_REFERENCE,
	ERROR_COULD_NOT_INIT_SWR_CONTEXT,
	ERROR_NOT_CREATED_AUDIO_SAMPLE_BYTE_ARRAY,
	ERROR_PLAYING_AUDIO,
	ERROR_WHILE_LOCING_BITMAP,

	ERROR_COULD_NOT_JOIN_PTHREAD,
	ERROR_COULD_NOT_INIT_PTHREAD_ATTR,
	ERROR_COULD_NOT_CREATE_PTHREAD,
	ERROR_COULD_NOT_DESTROY_PTHREAD_ATTR,
	ERROR_COULD_NOT_ALLOCATE_MEMORY,
};

#define AV_LOG_QUIET    -8
#define AV_LOG_PANIC     0
#define AV_LOG_FATAL     8
#define AV_LOG_ERROR    16
#define AV_LOG_WARNING  24
#define AV_LOG_INFO     32
#define AV_LOG_VERBOSE  40
#define AV_LOG_DEBUG    48

void ffmpeg_log_callback(void* avcl, int level, const char* fmt, va_list vl) {
	if (level > av_log_get_level())
		return;
	int andriod_level = ANDROID_LOG_DEFAULT;
	if (level > AV_LOG_DEBUG) {
		andriod_level = ANDROID_LOG_DEBUG;
	} else if (level > AV_LOG_VERBOSE) {
		andriod_level = ANDROID_LOG_VERBOSE;
	} else if (level > AV_LOG_INFO) {
		andriod_level = ANDROID_LOG_INFO;
	} else if (level > AV_LOG_WARNING) {
		andriod_level = ANDROID_LOG_WARN;
	} else if (level > AV_LOG_ERROR) {
		andriod_level = ANDROID_LOG_ERROR;
	} else if (level > AV_LOG_FATAL) {
		andriod_level = ANDROID_LOG_FATAL;
	} else if (level > AV_LOG_PANIC) {
		andriod_level = ANDROID_LOG_FATAL;
	} else if (level > AV_LOG_QUIET) {
		andriod_level = ANDROID_LOG_SILENT;
	}
	__android_log_vprint(andriod_level, "ffmpeg", fmt, vl);
}

void throw_exception(JNIEnv *env, const char * exception_class_path_name,
		const char *msg) {
	jclass newExcCls = (*env)->FindClass(env, exception_class_path_name);
	if (newExcCls == NULL) {
		assert(FALSE);
	}
	(*env)->ThrowNew(env, newExcCls, msg);
	(*env)->DeleteLocalRef(env, newExcCls);
}

void throw_interrupted_exception(JNIEnv *env, const char * msg) {
	throw_exception(env, interrupted_exception_class_path_name, msg);
}

void throw_runtime_exception(JNIEnv *env, const char * msg) {
	throw_exception(env, runtime_exception_class_path_name, msg);
}

void player_print_all_codecs() {
	AVCodec *p = NULL;
	LOGI(10, "player_print_all_codecs available codecs:");
	while ((p = av_codec_next(p)) != NULL) {
		player_print_codec_description(p);
	}
}

enum DecodeCheckMsg {
	DECODE_CHECK_MSG_STOP = 0, DECODE_CHECK_MSG_FLUSH,
};

QueueCheckFuncRet player_decode_queue_check_func(Queue *queue,
		struct DecoderData *decoderData, int *ret) {
	struct Player *player = decoderData->player;
	int stream_no = decoderData->stream_no;
	if (player->stop_streams[stream_no]) {
		*ret = DECODE_CHECK_MSG_STOP;
		return QUEUE_CHECK_FUNC_RET_SKIP;
	}
	if (player->flush_streams[stream_no]) {
		*ret = DECODE_CHECK_MSG_FLUSH;
		return QUEUE_CHECK_FUNC_RET_SKIP;
	}
	return QUEUE_CHECK_FUNC_RET_TEST;
}

void player_decode_audio_flush(struct DecoderData * decoder_data, JNIEnv * env) {
	struct Player *player = decoder_data->player;
	(*env)->CallVoidMethod(env, player->audio_track,
			player->audio_track_flush_method);
}
int player_decode_audio(struct DecoderData * decoder_data, JNIEnv * env,
		struct PacketData *packet_data) {
	int got_frame_ptr;
	struct Player *player = decoder_data->player;
	int stream_no = decoder_data->stream_no;
	AVCodecContext * ctx = player->input_codec_ctxs[stream_no];
	AVFrame * frame = player->input_frames[stream_no];

	LOGI(3, "player_decode_audio decoding");
	AVPacket *packet = packet_data->packet;
	int len = avcodec_decode_audio4(ctx, frame, &got_frame_ptr, packet);

	int64_t pts = packet->pts;

	if (len < 0) {
		LOGE(1, "Fail decoding audio %d\n", len);
		return -ERROR_WHILE_DECODING_VIDEO;
	}
	if (!got_frame_ptr) {
		LOGI(10, "player_decode_audio Audio frame not finished\n");
		return 0;
	}

	int original_data_size = av_samples_get_buffer_size(NULL, ctx->channels,
			frame->nb_samples, ctx->sample_fmt, 1);
	uint8_t *audio_buf;
	int data_size;

	if (player->swr_context != NULL) {
		uint8_t *out[] = { player->audio_buf2 };

		int sample_per_buffer_divider = player->audio_track_channel_count
				* av_get_bytes_per_sample(player->audio_track_format);

		int len2 = swr_convert(player->swr_context, out,
				sizeof(player->audio_buf2) / sample_per_buffer_divider,
				frame->data, frame->nb_samples);
		if (len2 < 0) {
			LOGE(1, "Could not resample frame");
			return -ERROR_COULD_NOT_RESAMPLE_FRAME;
		}
		if (len2 == sizeof(player->audio_buf2) / sample_per_buffer_divider) {
			LOGI(1, "warning: audio buffer is probably too small\n");
			swr_init(player->swr_context);
		}
		audio_buf = player->audio_buf2;
		data_size = len2 * sample_per_buffer_divider;
	} else {
		audio_buf = frame->data[0];
		data_size = original_data_size;
	}

	LOGI(10, "player_decode_audio Decoded audio frame\n");

	int err;
	if ((err = player_write_audio(decoder_data, env, pts, audio_buf, data_size,
			original_data_size))) {
		LOGE(1, "Could not write frame");
		return err;
	}
	return 0;
}
void player_decode_video_flush(struct DecoderData * decoder_data, JNIEnv * env) {
	struct Player *player = decoder_data->player;
	LOGI(2, "player_decode_video_flush flushing");

#ifdef SUBTITLES
	if (player->subtitle_stream_no >= 0) {
		struct SubtitleElem * subtitle = NULL;
		while ((subtitle = queue_pop_start_already_locked_non_block(
				player->subtitles_queue)) != NULL) {
			avsubtitle_free(&subtitle->subtitle);
			queue_pop_finish_already_locked(player->subtitles_queue,
					&player->mutex_queue, &player->cond_queue);
		}
	}
#endif
}
#ifdef SUBTITLES
static void player_print_subtitle(AVSubtitle *sub, int64_t time) {
	if (3 > LOG_LEVEL)
		return;
	LOGI(3, "player_decode_subtitles pts: %fd", time / 1000000.0);
	LOGI(3, "player_decode_subtitles sub.format: %d", sub->format);
	LOGI(3,
			"player_decode_subtitles sub.start_display_time: %d",
			sub->start_display_time);
	LOGI(3,
			"player_decode_subtitles sub.end_display_time: %d",
			sub->end_display_time);
	LOGI(3, "player_decode_subtitles sub.num_rects: %d", sub->num_rects);
	int i;
	for (i = 0; i < sub->num_rects; ++i) {
		AVSubtitleRect * rect = sub->rects[i];
		LOGI(3,
				"player_decode_subtitles --rect->(x,y,w,h) = (%d,%d,%d,%d)",
				rect->x, rect->y, rect->w, rect->h);
		LOGI(3,
				"player_decode_subtitles --rect->nb_colors = %d",
				rect->nb_colors);
		LOGI(3, "player_decode_subtitles --rect->text = %s", rect->text);
		LOGI(3, "player_decode_subtitles --rect->ass = %s", rect->ass);
		LOGI(3,
				"player_decode_subtitles --rect->forced = %s",
				rect->forced ? "true" : "false");
		char *type = "undefined";
		if (rect->type == SUBTITLE_NONE) {
			type = "none";
		} else if (rect->type == SUBTITLE_BITMAP) {
			type = "bitmap";
		} else if (rect->type == SUBTITLE_TEXT) {
			type = "text";
		} else if (rect->type == SUBTITLE_ASS) {
			type = "ass";
		}
		LOGI(3, "player_decode_subtitles --rect->type = %s", type);
	}
	LOGI(3, "player_decode_subtitles sub.pts: %d", sub->pts);
}
void player_decode_subtitles_flush(struct DecoderData * decoder_data,
		JNIEnv * env) {
	struct Player *player = decoder_data->player;

	pthread_mutex_lock(&player->mutex_ass);
	ass_flush_events(player->ass_track);
	pthread_mutex_unlock(&player->mutex_ass);

	if (player->subtitle_stream_no >= 0) {
		struct SubtitleElem * subtitle = NULL;
		while ((subtitle = queue_pop_start_already_locked_non_block(
				player->subtitles_queue)) != NULL) {
			avsubtitle_free(&subtitle->subtitle);
			queue_pop_finish_already_locked(player->subtitles_queue,
					&player->mutex_queue, &player->cond_queue);
		}
	}
}

static void ass_msg_callback(int level, const char *fmt, va_list va, void *data) {
	if (level > 100)
		return;
	__android_log_vprint(ANDROID_LOG_INFO, "libass", fmt, va);
}

//#define MAX_PRINT_LEN 1024
//static char print_buff[MAX_PRINT_LEN*2+1];

//static void player_log_hex(char *log, char *data, int len) {
//	int i;
//	if (len > MAX_PRINT_LEN)
//		exit(1);
//	for (i = 0; i < len; ++i)
//		sprintf(&print_buff[i*2], "%02X", (unsigned char)data[i]);
//	LOGI(10, log, print_buff);
//}

int player_decode_subtitles(struct DecoderData * decoder_data, JNIEnv * env,
		struct PacketData *packet_data) {
	struct Player *player = decoder_data->player;
	int stream_no = decoder_data->stream_no;
	AVCodecContext * ctx = player->input_codec_ctxs[stream_no];
	AVStream * stream = player->input_streams[stream_no];
	AVPacket *packet = packet_data->packet;
	int to_write;
	int interrupt_ret;

	struct AVSubtitle sub;
	int got_sub_ptr;

	int ret = avcodec_decode_subtitle2(ctx, &sub, &got_sub_ptr, packet);
	if (ret < 0) {
		LOGE(1, "player_decode_subtitles Fail decoding %d", ret);
		// ignore error - continue
		return ERROR_NO_ERROR;
	}
	if (!got_sub_ptr) {
		LOGI(10, "player_decode_subtitles frame not finished");
		return ERROR_NO_ERROR;
	}

	int rect_no;
	pthread_mutex_lock(&player->mutex_ass);
	for (rect_no = 0; rect_no < sub.num_rects; ++rect_no) {
		AVSubtitleRect *rect = sub.rects[rect_no];
		if (rect->type != SUBTITLE_ASS)
			continue;
		LOGI(3, "player_decode_video_subtitles: ass: %s", rect->ass);
		ass_process_data(player->ass_track, rect->ass, strlen(rect->ass));
	}
	pthread_mutex_unlock(&player->mutex_ass);

	int64_t time = av_rescale_q(packet->pts, stream->time_base, AV_TIME_BASE_Q);
//	double pts = 0;
//	if (packet->pts != AV_NOPTS_VALUE)
//		pts = av_q2d(stream->time_base) * packet->pts;

	player_print_subtitle(&sub, time);

	pthread_mutex_lock(&player->mutex_queue);
	struct SubtitleElem *elem = queue_push_start_already_locked(
			player->subtitles_queue, &player->mutex_queue, &player->cond_queue,
			&to_write, (QueueCheckFunc) player_decode_queue_check_func,
			decoder_data, (void **) &interrupt_ret);
	if (elem == NULL) {
		if (interrupt_ret == DECODE_CHECK_MSG_STOP) {
			LOGI(2, "player_decode_video push stop");
			pthread_mutex_unlock(&player->mutex_queue);
			return 0;
		} else if (interrupt_ret == DECODE_CHECK_MSG_FLUSH) {
			LOGI(2, "player_decode_video push flush");
			pthread_mutex_unlock(&player->mutex_queue);
			return 0;
		} else {
			assert(FALSE);
		}
	}
	pthread_mutex_unlock(&player->mutex_queue);

	elem->subtitle = sub;
	elem->start_time = time + (sub.start_display_time * 1000ll);
	elem->stop_time = time + (sub.end_display_time * 1000ll);

	queue_push_finish(player->subtitles_queue, &player->mutex_queue,
			&player->cond_queue, to_write);
	return ERROR_NO_ERROR;
}
#endif // SUBTITLES

void player_sws_context_free(struct Player *player) {
	if (player->sws_context != NULL) {
		sws_freeContext(player->sws_context);
		player->sws_context = NULL;
	}
}

inline int64_t player_get_current_video_time(struct Player *player) {
	if (player->pause) {
		return player->pause_time - player->start_time;
	} else {
		int64_t current_time = av_gettime();
		return current_time - player->start_time;
	}
}

void player_update_current_time(struct State *state,
		int64_t current_time,
		int64_t video_duration,
		int is_finished) {
	struct Player *player = state->player;
	jboolean jis_finished = is_finished ? JNI_TRUE : JNI_FALSE;

	LOGI(4, "player_update_current_time: %f/%f",
			current_time/1000000.0, video_duration/1000000.0);
	(*state->env)->CallVoidMethod(state->env, player->thiz,
			player->player_on_update_time_method, current_time,
			video_duration, jis_finished);
}

void player_update_time(struct State *state, int is_finished) {
	struct Player *player = state->player;
	int inform_user = FALSE;

	pthread_mutex_lock(&player->mutex_queue);
	int64_t current_video_time = player_get_current_video_time(player);
	int64_t time_diff = player->last_updated_time - current_video_time;

	if (is_finished) {
		inform_user = TRUE;
	}

	if (time_diff > 500000ll || time_diff < -500000ll) {
		player->last_updated_time = current_video_time;
		inform_user = TRUE;
	}

	int64_t video_duration = player->video_duration;
	pthread_mutex_unlock(&player->mutex_queue);

	LOGI(6, "player_update_time: %f/%f",
			current_video_time/1000000.0, video_duration/1000000.0);

	if (inform_user) {
		// because video duration can be estimate we have to ensure that
		// it will not be smaller than current time
		if (current_video_time > video_duration) {
			video_duration = current_video_time;
		}
		player_update_current_time(state, current_video_time,
				video_duration, is_finished);
	}
}

enum WaitFuncRet player_wait_for_frame(struct Player *player, int64_t stream_time,
		int stream_no) {
	LOGI(6, "player_wait_for_frame[%d] start", stream_no);
	pthread_mutex_lock(&player->mutex_queue);
	int ret = WAIT_FUNC_RET_OK;
	while (1) {
		if (player->flush_streams[stream_no]) {
			LOGI(3, "player_wait_for_frame[%d] flush_streams", stream_no);
			ret = WAIT_FUNC_RET_SKIP;
			break;
		}
		if (player->stop_streams[stream_no]) {
			LOGI(3, "player_wait_for_frame[%d] stop_streams", stream_no);
			ret = WAIT_FUNC_RET_SKIP;
			break;
		}
		if (player->pause) {
			pthread_cond_wait(&player->cond_queue, &player->mutex_queue);
			continue;
		}

		int64_t current_video_time = player_get_current_video_time(player);

		LOGI(8,
				"player_wait_for_frame[%d = %s] = (%f) - (%f)",
				stream_no,
				player->video_stream_no == stream_no ? "Video" : "Audio",
						stream_time, current_video_time/1000000.0);

		int64_t sleep_time = stream_time - current_video_time;

		LOGI(8,
				"player_wait_for_frame[%d] Waiting for frame: sleeping: %" SCNd64,
				stream_no, sleep_time);

		if (sleep_time < -300000ll) {
			// 300 ms late
			int64_t new_value = player->start_time - sleep_time;

			LOGI(4,
					"player_wait_for_frame[%d] correcting %f to %f because late",
					stream_no, (av_gettime() - player->start_time) / 1000000.0,
					(av_gettime() - new_value) / 1000000.0);

			player->start_time = new_value;
			pthread_cond_broadcast(&player->cond_queue);
		}

		if (sleep_time <= MIN_SLEEP_TIME_MS) {
			// We do not need to wait if time is slower then minimal sleep time
			break;
		}

		if (sleep_time > 500000ll) {
			// if sleep time is bigger then 500ms just sleep this 500ms
			// and check everything again
			sleep_time = 500000ll;
		}

		int timeout_ret = pthread_cond_timeout_np(&player->cond_queue,
				&player->mutex_queue, sleep_time/1000ll);
		if (timeout_ret == ETIMEDOUT) {
			// nothing special probably it is time ready to display
			// but for sure check everything again
			LOGI(9, "player_wait_for_frame[%d] timeout", stream_no);
		}
	}

	// just go further
	LOGI(6, "player_wait_for_frame[%d] finish[%d]", stream_no, ret);
	pthread_mutex_unlock(&player->mutex_queue);
	return ret;
}

int player_decode_video(struct DecoderData * decoder_data, JNIEnv * env,
		struct PacketData *packet_data) {
	int got_frame_ptr;
	struct Player *player = decoder_data->player;
	int stream_no = decoder_data->stream_no;
	AVCodecContext * ctx = player->input_codec_ctxs[stream_no];
	AVFrame * frame = player->input_frames[stream_no];
	AVStream * stream = player->input_streams[stream_no];
	int interrupt_ret;
	int to_write;
	int err = 0;
	AVFrame *rgb_frame = player->rgb_frame;
	ANativeWindow_Buffer buffer;
	ANativeWindow * window;

#ifdef MEASURE_TIME
	struct timespec timespec1, timespec2, diff;
#endif // MEASURE_TIME
	LOGI(10, "player_decode_video decoding");
	int frameFinished;

#ifdef MEASURE_TIME
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &timespec1);
#endif // MEASURE_TIME
	int ret = avcodec_decode_video2(ctx, frame, &frameFinished,
			packet_data->packet);

#ifdef MEASURE_TIME
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &timespec2);
	diff = timespec_diff(timespec1, timespec2);
	LOGI(3, "decode_video timediff: %d.%9ld", diff.tv_sec, diff.tv_nsec);
#endif // MEASURE_TIME

	if (ret < 0) {
		LOGE(1, "player_decode_video Fail decoding video %d\n", ret);
		return -ERROR_WHILE_DECODING_VIDEO;
	}
	if (!frameFinished) {
		LOGI(10, "player_decode_video Video frame not finished\n");
		return 0;
	}

	// saving in buffer converted video frame
	LOGI(7, "player_decode_video copy wait");

#ifdef MEASURE_TIME
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &timespec1);
#endif // MEASURE_TIME

	pthread_mutex_lock(&player->mutex_queue);
	window = player->window;
	if (window == NULL) {
		pthread_mutex_unlock(&player->mutex_queue);
		goto skip_frame;
	}
	ANativeWindow_setBuffersGeometry(window, ctx->width, ctx->height,
			WINDOW_FORMAT_RGBA_8888);
	if (ANativeWindow_lock(window, &buffer, NULL) != 0) {
		pthread_mutex_unlock(&player->mutex_queue);
		goto skip_frame;
	}
	pthread_mutex_unlock(&player->mutex_queue);

	int format = buffer.format;
	if (format < 0) {
		LOGE(1, "Could not get window format")
	}
	enum PixelFormat out_format;
	if (format == WINDOW_FORMAT_RGBA_8888) {
		out_format = PIX_FMT_RGBA;
		LOGI(6, "Format: WINDOW_FORMAT_RGBA_8888");
	} else if (format == WINDOW_FORMAT_RGBX_8888) {
		out_format = PIX_FMT_RGB0;
		LOGE(1, "Format: WINDOW_FORMAT_RGBX_8888 (not supported)");
	} else if (format == WINDOW_FORMAT_RGB_565) {
		out_format = PIX_FMT_RGB565;
		LOGE(1, "Format: WINDOW_FORMAT_RGB_565 (not supported)");
	} else {
		LOGE(1, "Unknown window format");
	}

	avpicture_fill((AVPicture *) rgb_frame, buffer.bits, out_format,
			buffer.width, buffer.height);
	rgb_frame->data[0] = buffer.bits;
	if (format == WINDOW_FORMAT_RGBA_8888) {
		rgb_frame->linesize[0] = buffer.stride * 4;
	} else {
		LOGE(1, "Unknown window format");
	}
	LOGI(6,
			"Buffer: width: %d, height: %d, stride: %d",
			buffer.width, buffer.height, buffer.stride);
	int i = 0;

#ifdef MEASURE_TIME
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &timespec2);
	diff = timespec_diff(timespec1, timespec2);
	LOGI(1,
			"lockPixels and fillimage timediff: %d.%9ld", diff.tv_sec, diff.tv_nsec);
#endif // MEASURE_TIME
#ifdef MEASURE_TIME
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &timespec1);
#endif // MEASURE_TIME
	LOGI(7, "player_decode_video copying...");
	AVFrame * out_frame;
	int rescale;
	if (ctx->width == buffer.width && ctx->height == buffer.height) {
		// This always should be true
		out_frame = rgb_frame;
		rescale = FALSE;
	} else {
		out_frame = player->tmp_frame2;
		rescale = TRUE;
	}

	if (ctx->pix_fmt == PIX_FMT_YUV420P) {
		__I420ToARGB(frame->data[0], frame->linesize[0], frame->data[2],
				frame->linesize[2], frame->data[1], frame->linesize[1],
				out_frame->data[0], out_frame->linesize[0], ctx->width,
				ctx->height);
	}
	if (ctx->pix_fmt == PIX_FMT_NV12) {
		__NV21ToARGB(frame->data[0], frame->linesize[0], frame->data[1],
				frame->linesize[1], out_frame->data[0], out_frame->linesize[0],
				ctx->width, ctx->height);
	} else {
		LOGI(3, "Using slow conversion: %d ", ctx->pix_fmt);
		struct SwsContext *sws_context = player->sws_context;
		sws_context = sws_getCachedContext(sws_context, ctx->width, ctx->height,
				ctx->pix_fmt, ctx->width, ctx->height, out_format,
				SWS_FAST_BILINEAR, NULL, NULL, NULL);
		player->sws_context = sws_context;
		if (sws_context == NULL) {
			LOGE(1, "could not initialize conversion context from: %d"
			", to :%d\n", ctx->pix_fmt, out_format);
			// TODO some error
		}
		sws_scale(sws_context, (const uint8_t * const *) frame->data,
				frame->linesize, 0, ctx->height, out_frame->data,
				out_frame->linesize);
	}

	if (rescale) {
		// Never occurs
		__ARGBScale(out_frame->data[0], out_frame->linesize[0], ctx->width,
				ctx->height, rgb_frame->data[0], rgb_frame->linesize[0],
				buffer.width, buffer.height, __kFilterNone);
		out_frame = rgb_frame;
	}

	int64_t pts = av_frame_get_best_effort_timestamp(frame);
	if (pts == AV_NOPTS_VALUE) {
		pts = 0;
	}
	int64_t time = av_rescale_q(pts, stream->time_base, AV_TIME_BASE_Q);
	LOGI(10,
			"player_decode_video Decoded video frame: %f, time_base: %" SCNd64,
			time/1000000.0, pts);
	player_wait_for_frame(player, time, stream_no);


#ifdef SUBTITLES

	if ((player->subtitle_stream_no >= 0)) {
//		double timeDouble = (double) pts * av_q2d(stream->time_base);
		pthread_mutex_lock(&player->mutex_queue);
		struct SubtitleElem * subtitle = NULL;
		// there is no subtitles in this video
		for (;;) {
			subtitle = queue_pop_start_already_locked_non_block(
					player->subtitles_queue);
			LOGI(5, "player_decode_video reading subtitle");
			if (subtitle == NULL) {
				LOGI(5, "player_decode_video no more subtitles found");
				break;
			}
			if (subtitle->stop_time >= time)
				break;
			avsubtitle_free(&subtitle->subtitle);
			subtitle = NULL;
			LOGI(5, "player_decode_video discarding old subtitle");
			queue_pop_finish_already_locked(player->subtitles_queue,
					&player->mutex_queue, &player->cond_queue);
		}

		if (subtitle != NULL) {
			if (subtitle->start_time > time) {
				LOGI(5,
						"player_decode_video rollback too new subtitle: %f > %f",
						subtitle->start_time/1000000.0, time/1000000.0);
				queue_pop_roll_back_already_locked(player->subtitles_queue,
						&player->mutex_queue, &player->cond_queue);
				subtitle = NULL;
			}
		}

		pthread_mutex_unlock(&player->mutex_queue);


		/* libass stores an RGBA color in the format RRGGBBAA,
		 * where AA is the transparency level */
		if (subtitle != NULL) {
			LOGI(5, "player_decode_video blend subtitle");
			int i;
			struct AVSubtitle *sub = &subtitle->subtitle;
			for (i = 0; i < sub->num_rects; i++) {
				AVSubtitleRect *rect = sub->rects[i];
				if (rect->type != SUBTITLE_BITMAP) {
					continue;
				}
				LOGI(5, "player_decode_video blending subtitle");
				blend_subrect_rgba((AVPicture *) out_frame, rect, buffer.width,
						buffer.height, out_format);
			}
		}
		int64_t time_ms = time / 1000;

		LOGI(3,
				"player_decode_video_subtitles: trying to find subtitles in : %" SCNd64,
				time_ms);
		pthread_mutex_lock(&player->mutex_ass);
		ASS_Image *image = ass_render_frame(player->ass_renderer,
				player->ass_track, time_ms, NULL);
		for (; image != NULL; image = image->next) {
			LOGI(3,
					"player_decode_video_subtitles: printing subtitles in : %" SCNd64,
					time_ms);
			blend_ass_image((AVPicture *) out_frame, image, buffer.width,
					buffer.height, out_format);
		}
		pthread_mutex_unlock(&player->mutex_ass);

		pthread_mutex_lock(&player->mutex_queue);
		if (subtitle != NULL) {
			LOGI(5, "player_decode_video rollback wroten subtitle");
			queue_pop_roll_back_already_locked(player->subtitles_queue, &player->mutex_queue,
					&player->cond_queue);
			subtitle = NULL;
		}
		pthread_mutex_unlock(&player->mutex_queue);
	}
#endif // SUBTITLES

	ANativeWindow_unlockAndPost(window);
skip_frame:
	return err;
}

void * player_decode(void * data) {

	int err = ERROR_NO_ERROR;
	struct DecoderData *decoder_data = data;
	struct Player *player = decoder_data->player;
	int stream_no = decoder_data->stream_no;
	Queue *queue = player->packets[stream_no];
	AVCodecContext * ctx = player->input_codec_ctxs[stream_no];
	enum AVMediaType codec_type = ctx->codec_type;

	int stop = FALSE;
	JNIEnv * env;
	char thread_title[256];
	sprintf(thread_title, "FFmpegDecode[%d]", stream_no);

	JavaVMAttachArgs thread_spec = { JNI_VERSION_1_4, thread_title, NULL };

	jint ret = (*player->get_javavm)->AttachCurrentThread(player->get_javavm,
			&env, &thread_spec);
	if (ret || env == NULL) {
		err = -ERROR_COULD_NOT_ATTACH_THREAD;
		goto end;
	}

	for (;;) {
		LOGI(10, "player_decode waiting for frame[%d]", stream_no);
		int interrupt_ret;
		struct PacketData *packet_data;
		pthread_mutex_lock(&player->mutex_queue);
		pop: packet_data = queue_pop_start_already_locked(&queue,
				&player->mutex_queue, &player->cond_queue,
				(QueueCheckFunc) player_decode_queue_check_func, decoder_data,
				(void **) &interrupt_ret);

		if (packet_data == NULL) {
			if (interrupt_ret == DECODE_CHECK_MSG_FLUSH) {
				goto flush;
			} else if (interrupt_ret == DECODE_CHECK_MSG_STOP) {
				goto stop;
			} else {
				assert(FALSE);
			}
		}
		pthread_mutex_unlock(&player->mutex_queue);
		LOGI(10, "player_decode decoding frame[%d]", stream_no);
		if (packet_data->end_of_stream) {
			LOGI(10, "player_decode read end of stream");
		}

#ifdef MEASURE_TIME
		struct timespec timespec1, timespec2;
		clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &timespec1);
#endif // MEASURE_TIME
		if (codec_type == AVMEDIA_TYPE_AUDIO) {
			err = player_decode_audio(decoder_data, env, packet_data);
		} else if (codec_type == AVMEDIA_TYPE_VIDEO) {
			err = player_decode_video(decoder_data, env, packet_data);
		} else
#ifdef SUBTITLES
		if (codec_type == AVMEDIA_TYPE_SUBTITLE) {
			err = player_decode_subtitles(decoder_data, env, packet_data);
		} else
#endif // SUBTITLES
		{
			assert(FALSE);
		}

		struct State state = {player: player, env:env};
		player_update_time(&state, packet_data->end_of_stream);

#ifdef MEASURE_TIME
		char * type = "unknown";
		if (codec_type == AVMEDIA_TYPE_AUDIO) {
			type = "audio";
		} else if (codec_type == AVMEDIA_TYPE_VIDEO) {
			type = "video";
		} else if (codec_type == AVMEDIA_TYPE_SUBTITLE) {
			type = "subtitle";
		}
		clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &timespec2);
		struct timespec diff = timespec_diff(timespec1, timespec2);
		LOGI(7,
				"decode timediff (%s): %d.%9ld", type, diff.tv_sec, diff.tv_nsec);
#endif // MEASURE_TIME
		if (!packet_data->end_of_stream) {
			av_free_packet(packet_data->packet);
		}
		queue_pop_finish(queue, &player->mutex_queue, &player->cond_queue);
		if (err < 0) {
			pthread_mutex_lock(&player->mutex_queue);
			goto stop;
		}

		goto end_loop;

		stop:
		LOGI(2, "player_decode stop[%d]", stream_no);
		stop = TRUE;

		flush:
		LOGI(2, "player_decode flush[%d]", stream_no);
		struct PacketData *to_free;
		while ((to_free = queue_pop_start_already_locked_non_block(queue))
				!= NULL) {
			if (!to_free->end_of_stream) {
				av_free_packet(to_free->packet);
			}
			queue_pop_finish_already_locked(queue, &player->mutex_queue,
					&player->cond_queue);
		}
		LOGI(2, "player_decode flushing playback[%d]", stream_no);

		if (codec_type == AVMEDIA_TYPE_AUDIO) {
			player_decode_audio_flush(decoder_data, env);
		} else if (codec_type == AVMEDIA_TYPE_VIDEO) {
			player_decode_video_flush(decoder_data, env);
		} else
#ifdef SUBTITLES
		if (codec_type == AVMEDIA_TYPE_SUBTITLE) {
			player_decode_subtitles_flush(decoder_data, env);
		} else
#endif // SUBTITLES
		{
			assert(FALSE);
		}
		LOGI(2, "player_decode flushed playback[%d]", stream_no);

		if (stop) {
			LOGI(2, "player_decode stopping stream");
			player->stop_streams[stream_no] = FALSE;
			pthread_cond_broadcast(&player->cond_queue);
			pthread_mutex_unlock(&player->mutex_queue);
			goto detach_current_thread;
		} else {
			LOGI(2, "player_decode flush stream[%d]", stream_no);
			player->flush_streams[stream_no] = FALSE;
			pthread_cond_broadcast(&player->cond_queue);
			goto pop;
		}
		end_loop: continue;
	}

	detach_current_thread: ret = (*player->get_javavm)->DetachCurrentThread(
			player->get_javavm);
	if (ret && !err)
		err = ERROR_COULD_NOT_DETACH_THREAD;

	end: free(decoder_data);
	decoder_data = NULL;

	// TODO do something with err
	return NULL;
}

enum ReadFromStreamCheckMsg {
	READ_FROM_STREAM_CHECK_MSG_STOP = 0, READ_FROM_STREAM_CHECK_MSG_SEEK,
};

QueueCheckFuncRet player_read_from_stream_check_func(Queue *queue,
		struct Player *player, int *ret) {
	if (player->stop) {
		*ret = READ_FROM_STREAM_CHECK_MSG_STOP;
		return QUEUE_CHECK_FUNC_RET_SKIP;
	}
	if (player->seek_position != DO_NOT_SEEK) {
		*ret = READ_FROM_STREAM_CHECK_MSG_SEEK;
		return QUEUE_CHECK_FUNC_RET_SKIP;
	}
	return QUEUE_CHECK_FUNC_RET_TEST;
}

static void player_assign_to_no_boolean_array(struct Player *player, int* array,
		int value) {
	int capture_streams_no = player->caputre_streams_no;
	int stream_no;
	for (stream_no = 0; stream_no < capture_streams_no; ++stream_no) {
		array[stream_no] = value;
	}
}
static int player_if_all_no_array_elements_has_value(struct Player *player,
		int *array, int value) {
	int capture_streams_no = player->caputre_streams_no;
	int stream_no;
	for (stream_no = 0; stream_no < capture_streams_no; ++stream_no) {
		if (array[stream_no] != value)
			return FALSE;
	}
	return TRUE;
}

void * player_read_from_stream(void *data) {
	struct Player *player = (struct Player *) data;
	int err = ERROR_NO_ERROR;

	AVPacket packet, *pkt = &packet;
	int64_t seek_target;
	JNIEnv * env;
	Queue *queue;
	int seek_input_stream_number;
	AVStream * seek_input_stream;
	struct PacketData *packet_data;
	int to_write;
	int interrupt_ret;
	JavaVMAttachArgs thread_spec = { JNI_VERSION_1_4, "FFmpegReadFromStream",
			NULL };

	jint ret = (*player->get_javavm)->AttachCurrentThread(player->get_javavm,
			&env, &thread_spec);
	if (ret) {
		err = ERROR_COULD_NOT_ATTACH_THREAD;
		goto end;
	}

	for (;;) {
		int ret = av_read_frame(player->input_format_ctx, pkt);
		if (ret < 0) {
			pthread_mutex_lock(&player->mutex_queue);
			LOGI(3, "player_read_from_stream stream end");
			queue = player->packets[player->video_stream_no];
			packet_data = queue_push_start_already_locked(queue,
					&player->mutex_queue, &player->cond_queue, &to_write,
					(QueueCheckFunc) player_read_from_stream_check_func, player,
					(void **) &interrupt_ret);
			if (packet_data == NULL) {
				if (interrupt_ret == READ_FROM_STREAM_CHECK_MSG_STOP) {
					LOGI(2, "player_read_from_stream queue interrupt stop");
					goto exit_loop;
				} else if (interrupt_ret == READ_FROM_STREAM_CHECK_MSG_SEEK) {
					LOGI(2, "player_read_from_stream queue interrupt seek");
					goto seek_loop;
				} else {
					assert(FALSE);
				}
			}
			packet_data->end_of_stream = TRUE;
			LOGI(3, "player_read_from_stream sending end_of_stream packet");
			queue_push_finish_already_locked(queue, &player->mutex_queue,
					&player->cond_queue, to_write);

			for (;;) {
				if (player->stop)
					goto exit_loop;
				if (player->seek_position != DO_NOT_SEEK)
					goto seek_loop;
				pthread_cond_wait(&player->cond_queue, &player->mutex_queue);
			}
			pthread_mutex_unlock(&player->mutex_queue);
		}

		LOGI(8, "player_read_from_stream Read frame");
		pthread_mutex_lock(&player->mutex_queue);
		if (player->stop) {
			LOGI(4, "player_read_from_stream stopping");
			goto exit_loop;
		}
		if (player->seek_position != DO_NOT_SEEK) {
			goto seek_loop;
		}
		int stream_no;
		int caputre_streams_no = player->caputre_streams_no;

		parse_frame: queue = NULL;
		LOGI(3, "player_read_from_stream looking for stream")
		for (stream_no = 0; stream_no < caputre_streams_no; ++stream_no) {
			if (packet.stream_index
					== player->input_stream_numbers[stream_no]) {
				queue = player->packets[stream_no];
				LOGI(3, "player_read_from_stream stream found [%d]", stream_no);
			}
		}

		if (queue == NULL) {
			LOGI(3, "player_read_from_stream stream not found");
			goto skip_loop;
		}

		push_start:
		LOGI(10, "player_read_from_stream waiting for queue");
		packet_data = queue_push_start_already_locked(queue,
				&player->mutex_queue, &player->cond_queue, &to_write,
				(QueueCheckFunc) player_read_from_stream_check_func, player,
				(void **) &interrupt_ret);
		if (packet_data == NULL) {
			if (interrupt_ret == READ_FROM_STREAM_CHECK_MSG_STOP) {
				LOGI(2, "player_read_from_stream queue interrupt stop");
				goto exit_loop;
			} else if (interrupt_ret == READ_FROM_STREAM_CHECK_MSG_SEEK) {
				LOGI(2, "player_read_from_stream queue interrupt seek");
				goto seek_loop;
			} else {
				assert(FALSE);
			}
		}

		pthread_mutex_unlock(&player->mutex_queue);
		packet_data->end_of_stream = FALSE;
		*packet_data->packet = packet;

		if (av_dup_packet(packet_data->packet) < 0) {
			err = ERROR_WHILE_DUPLICATING_FRAME;
			pthread_mutex_lock(&player->mutex_queue);
			goto exit_loop;
		}

		queue_push_finish(queue, &player->mutex_queue, &player->cond_queue,
				to_write);

		goto end_loop;

		exit_loop:
		LOGI(3, "player_read_from_stream stop");
		av_free_packet(pkt);

		//request stream to stop
		player_assign_to_no_boolean_array(player, player->stop_streams, TRUE);
		pthread_cond_broadcast(&player->cond_queue);

		// wait for all stream stop
		while (!player_if_all_no_array_elements_has_value(player,
				player->stop_streams, FALSE)) {
			pthread_cond_wait(&player->cond_queue, &player->mutex_queue);
		}

		// flush internal buffers
		for (stream_no = 0; stream_no < caputre_streams_no; ++stream_no) {
			avcodec_flush_buffers(player->input_codec_ctxs[stream_no]);
		}

		pthread_mutex_unlock(&player->mutex_queue);
		goto detach_current_thread;

		seek_loop:
		// setting stream thet will be used as a base for seeking
		seek_input_stream_number =
				player->input_stream_numbers[player->video_stream_no];
		seek_input_stream = player->input_streams[player->video_stream_no];

		// getting seek target time in time_base value
		seek_target = av_rescale_q(
				player->seek_position, AV_TIME_BASE_Q,
				seek_input_stream->time_base);
		LOGI(3, "player_read_from_stream seeking to: "
		"%ds, time_base: %f", player->seek_position / 1000000.0, seek_target);

		// seeking
		if (av_seek_frame(player->input_format_ctx, seek_input_stream_number,
				seek_target, 0) < 0) {
			// seeking error - trying to play movie without it
			LOGE(1, "Error while seeking");
			player->seek_position = DO_NOT_SEEK;
			pthread_cond_broadcast(&player->cond_queue);
			goto parse_frame;
		}

		LOGI(3, "player_read_from_stream seeking success");

		int64_t current_time = av_gettime();
		player->start_time = current_time - player->seek_position;
		player->pause_time = current_time;

		// request stream to flush
		player_assign_to_no_boolean_array(player, player->flush_streams, TRUE);
		LOGI(3, "player_read_from_stream flushing audio")
		// flush audio buffer
		(*env)->CallVoidMethod(env, player->audio_track,
				player->audio_track_flush_method);
		LOGI(3, "player_read_from_stream flushed audio");
		pthread_cond_broadcast(&player->cond_queue);

		LOGI(3, "player_read_from_stream waiting for flush");

		// waiting for all stream flush
		while (!player_if_all_no_array_elements_has_value(player,
				player->flush_streams, FALSE)) {
			pthread_cond_wait(&player->cond_queue, &player->mutex_queue);
		}

		LOGI(3, "player_read_from_stream flushing internal codec bffers");
		// flush internal buffers
		for (stream_no = 0; stream_no < caputre_streams_no; ++stream_no) {
			avcodec_flush_buffers(player->input_codec_ctxs[stream_no]);
		}

		// finishing seeking
		player->seek_position = DO_NOT_SEEK;
		pthread_cond_broadcast(&player->cond_queue);
		LOGI(3, "player_read_from_stream ending seek");

		skip_loop: av_free_packet(pkt);
		pthread_mutex_unlock(&player->mutex_queue);

		end_loop: continue;
	}

	detach_current_thread: ret = (*player->get_javavm)->DetachCurrentThread(
			player->get_javavm);
	if (ret && !err)
		err = ERROR_COULD_NOT_DETACH_THREAD;

	end:

	// TODO do something with error valuse
	return NULL;
}

int player_write_audio(struct DecoderData *decoder_data, JNIEnv *env,
		int64_t pts, uint8_t *data, int data_size, int original_data_size) {
	struct Player *player = decoder_data->player;
	int stream_no = decoder_data->stream_no;
	int err = ERROR_NO_ERROR;
	int ret;
	AVCodecContext * c = player->input_codec_ctxs[stream_no];
	AVStream *stream = player->input_streams[stream_no];
	LOGI(10, "player_write_audio Writing audio frame")

	jbyteArray samples_byte_array = (*env)->NewByteArray(env, data_size);
	if (samples_byte_array == NULL) {
		err = -ERROR_NOT_CREATED_AUDIO_SAMPLE_BYTE_ARRAY;
		goto end;
	}

	if (pts != AV_NOPTS_VALUE) {
		player->audio_clock = av_rescale_q(pts, stream->time_base, AV_TIME_BASE_Q);
//				av_q2d(stream->time_base) * pts;
		LOGI(9, "player_write_audio - read from pts")
	} else {
		int64_t sample_time = original_data_size;
		sample_time *= 1000000ll;
		sample_time /= c->channels;
		sample_time /= c->sample_rate;
		sample_time /= av_get_bytes_per_sample(c->sample_fmt);
		player->audio_clock += sample_time;
		LOGI(9, "player_write_audio - added")
	}
	enum WaitFuncRet wait_ret = player_wait_for_frame(player,
			player->audio_clock, stream_no);
	if (wait_ret == WAIT_FUNC_RET_SKIP) {
		goto end;
	}

	LOGI(10, "player_write_audio Writing sample data")

	jbyte *jni_samples = (*env)->GetByteArrayElements(env, samples_byte_array,
			NULL);
	memcpy(jni_samples, data, data_size);
	(*env)->ReleaseByteArrayElements(env, samples_byte_array, jni_samples, 0);

	LOGI(10, "player_write_audio playing audio track");
	ret = (*env)->CallIntMethod(env, player->audio_track,
			player->audio_track_write_method, samples_byte_array, 0, data_size);
	jthrowable exc = (*env)->ExceptionOccurred(env);
	if (exc) {
		err = -ERROR_PLAYING_AUDIO;
		LOGE(3, "Could not write audio track: reason in exception");
		// TODO maybe release exc
		goto free_local_ref;
	}
	if (ret < 0) {
		err = -ERROR_PLAYING_AUDIO;
		LOGE(3,
				"Could not write audio track: reason: %d look in AudioTrack.write()", ret);
		goto free_local_ref;
	}

	free_local_ref:
	LOGI(10, "player_write_audio releasing local ref");
	(*env)->DeleteLocalRef(env, samples_byte_array);

	end: return err;
}

struct Player * player_get_player_field(JNIEnv *env, jobject thiz) {

	jfieldID m_native_layer_field = java_get_field(env, player_class_path_name,
			player_m_native_player);
	struct Player * player = (struct Player *) (*env)->GetIntField(env, thiz,
			m_native_layer_field);
	return player;
}

void *player_fill_packet(struct State *state) {
	struct PacketData *packet_data = malloc(sizeof(struct PacketData));
	if (packet_data == NULL) {
		return NULL;
	}
	packet_data->packet = malloc(sizeof(AVPacket));
	if (packet_data->packet == NULL) {
		free(packet_data);
		return NULL;
	}
	return packet_data;
}

void player_free_packet(struct State *player, struct PacketData *elem) {
	free(elem->packet);
	free(elem);
}

void *player_fill_subtitles_queue(struct DecoderState *decoder_state) {
	return malloc(sizeof(struct SubtitleElem));
}

void player_free_subtitles_queue(struct State *state, struct SubtitleElem *elem) {
	free(elem);
}

void player_open_stream_free(struct Player *player, int stream_no) {
	AVCodecContext ** ctx = &player->input_codec_ctxs[stream_no];
	if (*ctx != NULL) {
		avcodec_close(*ctx);
		*ctx = NULL;
	}
}

int player_open_stream(struct Player *player, AVCodecContext * ctx,
		const struct AVCodec **codec, int stream_no) {
	enum AVCodecID codec_id = ctx->codec_id;
	LOGI(3, "player_open_stream trying open: %d", codec_id);

	*codec = avcodec_find_decoder(codec_id);
	if (*codec == NULL) {
		LOGE(1,
				"player_set_data_source Could not find codec for id: %d",
				codec_id);
		return -ERROR_COULD_NOT_FIND_VIDEO_CODEC;
	}
	if (avcodec_open2(ctx, *codec, NULL) < 0) {
		LOGE(1, "Could not open codec");
		player_print_codec_description(*codec);
		*codec = NULL;
		return -ERROR_COULD_NOT_OPEN_VIDEO_CODEC;
	}
	LOGI(3,
			"player_open_stream opened: %d, name: %s, long_name: %s",
			codec_id, (*codec)->name, (*codec)->long_name);
	return 0;
}

void player_find_streams_free(struct Player *player) {
	int capture_streams_no = player->caputre_streams_no;
	int i;
	for (i = 0; i < capture_streams_no; ++i) {
		player_open_stream_free(player, i);
	}
	player->caputre_streams_no = 0;
	player->video_stream_no = -1;
	player->audio_stream_no = -1;
#ifdef SUBTITLES
	player->subtitle_stream_no = -1;
#endif // SUBTITLES
}

int player_try_open_stream(struct Player *player, enum AVMediaType codec_type,
		int stream_no) {
	if (stream_no < 0)
		return -1;
	if (stream_no >= player->input_format_ctx->nb_streams)
		return -1;

	AVStream *stream = player->input_format_ctx->streams[stream_no];
	AVCodecContext *ctx = stream->codec;
	if (ctx->codec_type != codec_type) {
		return -1;
	}

	const struct AVCodec * codec = ctx->codec;
	int err = player_open_stream(player, ctx, &codec, stream_no);
	if (err < 0) {
		return -1;
	}
	return stream_no;
}

int player_find_stream(struct Player *player, enum AVMediaType codec_type,
		int recommended_stream_no) {
	//find video stream
	int streams_no = player->caputre_streams_no;

	int err = ERROR_NO_ERROR;
	LOGI(3, "player_find_stream, type: %d", codec_type);

	int bn_stream = player_try_open_stream(player, codec_type,
			recommended_stream_no);

	if (bn_stream < 0) {
		int i;
		for (i = 0; i < player->input_format_ctx->nb_streams; i++) {
			bn_stream = player_try_open_stream(player, codec_type, i);
			if (bn_stream >= 0)
				break;
		}
	}

	if (bn_stream < 0) {
		return -1;
	}

	LOGI(3, "player_set_data_source 4");

	AVStream *stream = player->input_format_ctx->streams[bn_stream];
	player->input_streams[streams_no] = stream;
	player->input_codec_ctxs[streams_no] = stream->codec;
	player->input_stream_numbers[streams_no] = bn_stream;

	if (codec_type == AVMEDIA_TYPE_VIDEO) {
		LOGI(5,
				"player_set_data_source Video size is [%d x %d]",
				player->input_codec_ctxs[streams_no]->width,
				player->input_codec_ctxs[streams_no]->height);
	}

	player->caputre_streams_no += 1;
	return streams_no;
}

uint64_t player_find_layout_from_channels(int nb_channels) {
	int i;
	for (i = 0; i < FF_ARRAY_ELEMS(channel_android_layout_map); i++)
		if (nb_channels == channel_android_layout_map[i].nb_channels)
			return channel_android_layout_map[i].layout;
	return (uint64_t) 0;
}

void player_print_report_video_streams_free(JNIEnv* env, jobject thiz,
		struct Player *player) {
	if (player->player_set_stream_info_method != NULL)
		(*env)->CallVoidMethod(env, thiz, player->player_set_stream_info_method,
				NULL);
}

int player_print_report_video_streams(JNIEnv* env, jobject thiz,
		struct Player *player) {
	int i;
	int err = ERROR_NO_ERROR;
	jclass stream_info_class = (*env)->FindClass(env,
			stream_info_class_path_name);
	jmethodID stream_info_set_metadata_method = java_get_method(env,
			stream_info_class, steram_info_set_metadata);
	jmethodID stream_info_set_media_type_internal_method = java_get_method(env,
			stream_info_class, steram_info_set_media_type_internal);
	jmethodID stream_info_set_stream_number_method = java_get_method(env,
			stream_info_class, stream_info_set_stream_number);
	jmethodID stream_info_constructor = java_get_method(env, stream_info_class,
			empty_constructor);

	jclass hash_map_class = (*env)->FindClass(env, hash_map_class_path_name);
	jmethodID hash_map_constructor = java_get_method(env, hash_map_class,
			empty_constructor);

	jclass map_class = (*env)->FindClass(env, map_class_path_name);
	jmethodID map_put_method = java_get_method(env, map_class, map_put);

	jobjectArray array = (*env)->NewObjectArray(env,
			player->input_format_ctx->nb_streams, stream_info_class, NULL);
	if (array == NULL) {
		err = -ERROR_COULD_NOT_ALLOCATE_MEMORY;
		goto free_map_class;
	}
	for (i = 0;
			i < player->input_format_ctx->nb_streams && err == ERROR_NO_ERROR;
			i++) {
		AVStream *stream = player->input_format_ctx->streams[i];
		AVCodecContext *codec = stream->codec;
		AVDictionary *metadaat = stream->metadata;
		AVDictionaryEntry *tag = NULL;

		jobject stream_info = (*env)->NewObject(env, stream_info_class,
				stream_info_constructor);
		if (stream_info == NULL) {
			err = -ERROR_COULD_NOT_ALLOCATE_MEMORY;
			break;
		}

		jobject map = (*env)->NewObject(env, hash_map_class,
				hash_map_constructor);
		if (map == NULL) {
			err = -ERROR_COULD_NOT_ALLOCATE_MEMORY;
			goto loop_free_stream_info;
		}

		enum CodecType codec_type = CODEC_TYPE_UNKNOWN;
		if (codec->codec_type == AVMEDIA_TYPE_AUDIO) {
			codec_type = CODEC_TYPE_AUDIO;
		} else if (codec->codec_type == AVMEDIA_TYPE_VIDEO) {
			codec_type = CODEC_TYPE_VIDEO;
		} else if (codec->codec_type == AVMEDIA_TYPE_SUBTITLE) {
			codec_type = CODEC_TYPE_SUBTITLE;
		} else if (codec->codec_type == AVMEDIA_TYPE_ATTACHMENT) {
			codec_type = CODEC_TYPE_ATTACHMENT;
		} else if (codec->codec_type == AVMEDIA_TYPE_NB) {
			codec_type = CODEC_TYPE_NB;
		} else if (codec->codec_type == AVMEDIA_TYPE_DATA) {
			codec_type = CODEC_TYPE_DATA;
		}

		(*env)->CallVoidMethod(env, stream_info,
				stream_info_set_media_type_internal_method, (jint) codec_type);
		(*env)->CallVoidMethod(env, stream_info,
				stream_info_set_stream_number_method, (jint) i);

		while (err == ERROR_NO_ERROR
				&& (tag = av_dict_get(metadaat, "", tag, AV_DICT_IGNORE_SUFFIX))
						!= NULL) {
			jobject key = (*env)->NewStringUTF(env, tag->key);
			if (key == NULL) {
				err = -ERROR_COULD_NOT_ALLOCATE_MEMORY;
				break;
			}
			jobject value = (*env)->NewStringUTF(env, tag->value);
			if (value == NULL) {
				err = -ERROR_COULD_NOT_ALLOCATE_MEMORY;
				goto while_free_key;
			}

			jobject previous = (*env)->CallObjectMethod(env, map,
					map_put_method, key, value);
			if (previous != NULL) {
				(*env)->DeleteLocalRef(env, previous);
			}
			(*env)->DeleteLocalRef(env, value);
			while_free_key: (*env)->DeleteLocalRef(env, key);
		}

		(*env)->CallVoidMethod(env, stream_info,
				stream_info_set_metadata_method, map);
		(*env)->DeleteLocalRef(env, map);

		(*env)->SetObjectArrayElement(env, array, i, stream_info);
		loop_free_stream_info: (*env)->DeleteLocalRef(env, stream_info);
	}

	if (err == ERROR_NO_ERROR) {
		(*env)->CallVoidMethod(env, thiz, player->player_set_stream_info_method,
				array);
	}

	(*env)->DeleteLocalRef(env, array);
	free_map_class: (*env)->DeleteLocalRef(env, map_class);
	(*env)->DeleteLocalRef(env, hash_map_class);
	(*env)->DeleteLocalRef(env, stream_info_class);
	return err;
}

void player_print_video_informations(struct Player *player,
		const char *file_path) {
	if (LOG_LEVEL >= 3) {
		int i;
		av_dump_format(player->input_format_ctx, 0, file_path, FALSE);
		LOGI(3,
				"player_set_data_source Number of streams: %d",
				player->input_format_ctx->nb_streams);
		for (i = 0; i < player->input_format_ctx->nb_streams; i++) {
			AVStream *stream = player->input_format_ctx->streams[i];
			AVCodecContext *codec = stream->codec;
			LOGI(3, "- stream: %d", i);
			AVDictionary *metadaat = stream->metadata;
			AVDictionaryEntry *tag = NULL;
			LOGI(3, "-- metadata:")
			while ((tag = av_dict_get(metadaat, "", tag, AV_DICT_IGNORE_SUFFIX))
					!= NULL) {
				LOGI(3, "--- %s = %s", tag->key, tag->value);
			}
			LOGI(3, "-- codec_name: %s", codec->codec_name);
			char *codec_type = "other";
			if (codec->codec_type == AVMEDIA_TYPE_AUDIO) {
				codec_type = "audio";
			} else if (codec->codec_type == AVMEDIA_TYPE_VIDEO) {
				codec_type = "video";
			} else if (codec->codec_type == AVMEDIA_TYPE_SUBTITLE) {
				codec_type = "subtitle";
			} else if (codec->codec_type == AVMEDIA_TYPE_ATTACHMENT) {
				codec_type = "attachment";
			} else if (codec->codec_type == AVMEDIA_TYPE_NB) {
				codec_type = "nb";
			} else if (codec->codec_type == AVMEDIA_TYPE_DATA) {
				codec_type = "data";
			}
			LOGI(3, "-- codec_type: %s", codec_type);
		}
	}
}

int player_alloc_frames_free(struct Player *player) {
	int capture_streams_no = player->caputre_streams_no;
	int stream_no;
	for (stream_no = 0; stream_no < capture_streams_no; ++stream_no) {
		if (player->input_frames[stream_no] != NULL) {
			av_free(player->input_frames[stream_no]);
			player->input_frames[stream_no] = NULL;
		}
	}
	return 0;
}

int player_alloc_frames(struct Player *player) {
	int capture_streams_no = player->caputre_streams_no;
	int stream_no;
	for (stream_no = 0; stream_no < capture_streams_no; ++stream_no) {
		player->input_frames[stream_no] = avcodec_alloc_frame();
		if (player->input_frames[stream_no] == NULL) {
			return -ERROR_COULD_NOT_ALLOC_FRAME;
		}
	}
	return 0;
}

int player_alloc_queues(struct State *state) {
	struct Player *player = state->player;
	int capture_streams_no = player->caputre_streams_no;
	int stream_no;
	for (stream_no = 0; stream_no < capture_streams_no; ++stream_no) {
		player->packets[stream_no] = queue_init_with_custom_lock(50,
				(queue_fill_func) player_fill_packet,
				(queue_free_func) player_free_packet, state, state,
				&player->mutex_queue, &player->cond_queue);
		if (player->packets[stream_no] == NULL) {
			return -ERROR_COULD_NOT_PREPARE_PACKETS_QUEUE;
		}
	}
	return 0;
}
void player_alloc_queues_free(struct State *state) {
	struct Player *player = state->player;
	int capture_streams_no = player->caputre_streams_no;
	int stream_no;
	for (stream_no = 0; stream_no < capture_streams_no; ++stream_no) {
		if (player->packets[stream_no] != NULL) {
			queue_free(player->packets[stream_no], &player->mutex_queue,
					&player->cond_queue, state);
			player->packets[stream_no] = NULL;
		}
	}
}
#ifdef SUBTITLES
void player_prepare_subtitles_queue_free(struct State *state) {
	struct Player *player = state->player;
	if (player->subtitles_queue != NULL) {
		LOGI(7, "player_set_data_source free_subtitles_frames_queue");
		queue_free(player->subtitles_queue, &player->mutex_queue,
				&player->cond_queue, state);
		player->subtitles_queue = NULL;
	}
}

int player_prepare_subtitles_queue(struct DecoderState *decoder_state,
		struct State *state) {
	struct Player *player = decoder_state->player;

	player->subtitles_queue = queue_init_with_custom_lock(30,
			(queue_fill_func) player_fill_subtitles_queue,
			(queue_free_func) player_free_subtitles_queue, decoder_state, state,
			&player->mutex_queue, &player->cond_queue);
	if (player->subtitles_queue == NULL) {
		return -ERROR_COULD_NOT_PREPARE_SUBTITLES_QUEUE;
	}
	return 0;
}
#endif // SUBTITLES
void player_create_audio_track_free(struct Player *player, struct State *state) {
	if (player->swr_context != NULL) {
		swr_free(&player->swr_context);
		player->swr_context = NULL;
	}

	if (player->audio_track != NULL) {
		LOGI(7, "player_create_audio_track_free stop audio_track");
		(*state->env)->CallVoidMethod(state->env, player->audio_track,
				player->audio_track_pause_method);

		LOGI(7, "player_create_audio_track_free free audio_track");
		(*state->env)->DeleteGlobalRef(state->env, player->audio_track);
		player->audio_track = NULL;
	}
	if (player->audio_stream_no >= 0) {
		AVCodecContext ** ctx =
				&player->input_codec_ctxs[player->audio_stream_no];
		if (*ctx != NULL) {
			LOGI(7, "player_create_audio_track_free close");
			avcodec_close(*ctx);
			*ctx = NULL;
		}
	}
}

int player_create_audio_track(struct Player *player, struct State *state) {
	//creating audiotrack
	AVCodecContext * ctx = player->input_codec_ctxs[player->audio_stream_no];
	int sample_rate = ctx->sample_rate;
	int channels = ctx->channels;

	LOGI(3, "player_set_data_source 14");
	jobject audio_track = (*state->env)->CallObjectMethod(state->env,
			player->thiz, player->player_prepare_audio_track_method, sample_rate,
			channels);

	jthrowable exc = (*state->env)->ExceptionOccurred(state->env);
	if (exc) {
		return -ERROR_NOT_CREATED_AUDIO_TRACK;
	}
	if (audio_track == NULL) {
		return -ERROR_NOT_CREATED_AUDIO_TRACK;
	}

	LOGI(3, "player_set_data_source 15");
	player->audio_track = (*state->env)->NewGlobalRef(state->env, audio_track);
	(*state->env)->DeleteLocalRef(state->env, audio_track);
	if (player->audio_track == NULL) {
		return -ERROR_NOT_CREATED_AUDIO_TRACK_GLOBAL_REFERENCE;
	}

	player->audio_track_channel_count = (*state->env)->CallIntMethod(state->env,
			player->audio_track, player->audio_track_get_channel_count_method);
	int audio_track_sample_rate = (*state->env)->CallIntMethod(state->env,
			player->audio_track, player->audio_track_get_sample_rate_method);
	player->audio_track_format = AV_SAMPLE_FMT_S16;

	int64_t audio_track_layout = player_find_layout_from_channels(
			player->audio_track_channel_count);

	int64_t dec_channel_layout =
			(ctx->channel_layout
					&& ctx->channels
							== av_get_channel_layout_nb_channels(
									ctx->channel_layout)) ?
					ctx->channel_layout :
					av_get_default_channel_layout(ctx->channels);

	player->swr_context = NULL;
	if (ctx->sample_fmt != player->audio_track_format
			|| dec_channel_layout != audio_track_layout
			|| ctx->sample_rate != audio_track_sample_rate) {

		LOGI(3,
				"player_set_data_sourcd preparing conversion of %d Hz %s %d "
				"channels to %d Hz %s %d channels",
				ctx->sample_rate,
				av_get_sample_fmt_name(ctx->sample_fmt),
				ctx->channels,
				audio_track_sample_rate,
				av_get_sample_fmt_name(player->audio_track_format),
				player->audio_track_channel_count);
		player->swr_context = (struct SwrContext *) swr_alloc_set_opts(NULL,
				audio_track_layout, player->audio_track_format,
				audio_track_sample_rate, dec_channel_layout, ctx->sample_fmt,
				ctx->sample_rate, 0, NULL);

		if (!player->swr_context || swr_init(player->swr_context) < 0) {
			LOGE(1,
					"Cannot create sample rate converter for conversion "
					"of %d Hz %s %d channels to %d Hz %s %d channels!",
					ctx->sample_rate,
					av_get_sample_fmt_name(ctx->sample_fmt),
					ctx->channels,
					audio_track_sample_rate,
					av_get_sample_fmt_name(player->audio_track_format),
					player->audio_track_channel_count);
			return -ERROR_COULD_NOT_INIT_SWR_CONTEXT;
		}
	}
	return 0;
}

void player_get_video_duration(struct Player *player) {
	player->last_updated_time = -1;
	player->video_duration = 0;
	int i;

	for (i = 0; i < player->caputre_streams_no; ++i) {
		AVStream *stream = player->input_streams[i];
		if (stream->duration > 0) {
			player->video_duration = av_rescale_q(
					stream->duration, stream->time_base, AV_TIME_BASE_Q);
			LOGI(3,
					"player_set_data_source stream[%d] duration: %ld",
					i, stream->duration);
			return;
		}
	}
	if (player->input_format_ctx->duration != 0) {
		player->video_duration = player->input_format_ctx->duration;
		LOGI(3,
				"player_set_data_source video duration: %ld",
				player->input_format_ctx->duration)
		return;
	}

	for (i = 0; i < player->input_format_ctx->nb_streams; i++) {
		AVStream *stream = player->input_format_ctx->streams[i];
		if (stream->duration > 0) {
			player->video_duration = av_rescale_q(stream->duration,
					stream->time_base, AV_TIME_BASE_Q);
			LOGI(3,
					"player_set_data_source stream[%d] duration: %ld", i, stream->duration);
			return;
		}
	}
}

int player_start_decoding_threads(struct Player *player) {
	pthread_attr_t attr;
	int ret;
	int i;
	int err = 0;
	ret = pthread_attr_init(&attr);
	if (ret) {
		err = -ERROR_COULD_NOT_INIT_PTHREAD_ATTR;
		goto end;
	}
	for (i = 0; i < player->caputre_streams_no; ++i) {
		struct DecoderData * decoder_data = malloc(sizeof(decoder_data));
		*decoder_data = (struct DecoderData) {player: player, stream_no: i};
		ret = pthread_create(&player->decode_threads[i], &attr, player_decode,
				decoder_data);
		if (ret) {
			err = -ERROR_COULD_NOT_CREATE_PTHREAD;
			goto end;
		}
		player->decode_threads_created[i] = TRUE;
	}

	ret = pthread_create(&player->thread_player_read_from_stream, &attr,
			player_read_from_stream, player);
	if (ret) {
		err = -ERROR_COULD_NOT_CREATE_PTHREAD;
		goto end;
	}
	player->thread_player_read_from_stream_created = TRUE;

	end: ret = pthread_attr_destroy(&attr);
	if (ret) {
		if (!err) {
			err = ERROR_COULD_NOT_DESTROY_PTHREAD_ATTR;
		}
	}
	return err;
}

int player_start_decoding_threads_free(struct Player *player) {
	int err = 0;
	int ret;
	int i;
	if (player->thread_player_read_from_stream_created) {
		ret = pthread_join(player->thread_player_read_from_stream, NULL);
		player->thread_player_read_from_stream_created = FALSE;
		if (ret) {
			err = ERROR_COULD_NOT_JOIN_PTHREAD;
		}
	}

	for (i = 0; i < player->caputre_streams_no; ++i) {
		if (player->decode_threads_created[i]) {
			ret = pthread_join(player->decode_threads[i], NULL);
			player->decode_threads_created[i] = FALSE;
			if (ret) {
				err = ERROR_COULD_NOT_JOIN_PTHREAD;
			}
		}
	}
	return err;
}
void player_create_context_free(struct Player *player) {
	if (player->input_format_ctx != NULL) {
		LOGI(7, "player_set_data_source remove_context");
		av_free(player->input_format_ctx);
		player->input_format_ctx = NULL;
	}
}
int player_create_context(struct Player *player) {
	player->input_format_ctx = avformat_alloc_context();
	if (player->input_format_ctx == NULL) {
		LOGE(1, "Could not create AVContext\n");
		return -ERROR_COULD_NOT_CREATE_AVCONTEXT;
	}
	return 0;
}

void player_open_input_free(struct Player *player) {
	if (player->input_inited) {
		LOGI(7, "player_set_data_source close_file");
		avformat_close_input(&(player->input_format_ctx));
		player->input_inited = FALSE;
	}
}

int player_open_input(struct Player *player, const char *file_path,
		AVDictionary *dictionary) {
	int ret;
	if ((ret = avformat_open_input(&(player->input_format_ctx), file_path, NULL,
			&dictionary)) < 0) {
		char errbuf[128];
		const char *errbuf_ptr = errbuf;

		if (av_strerror(ret, errbuf, sizeof(errbuf)) < 0)
			errbuf_ptr = strerror(AVUNERROR(ret));

		LOGE(1,
				"player_set_data_source Could not open video file: %s (%d: %s)\n",
				file_path, ret, errbuf_ptr);
		return -ERROR_COULD_NOT_OPEN_VIDEO_FILE;
	}
	player->input_inited = TRUE;

	return ERROR_NO_ERROR;
}

void player_find_stream_info_free(struct Player *player) {
	// nothigng to do
}

int player_find_stream_info(struct Player *player) {
	LOGI(3, "player_set_data_source 2");
	// find video informations
	if (avformat_find_stream_info(player->input_format_ctx, NULL) < 0) {
		LOGE(1, "Could not open stream\n");
		return -ERROR_COULD_NOT_OPEN_STREAM;
	}
	return ERROR_NO_ERROR;
}

void player_play_prepare_free(struct Player *player) {
	pthread_mutex_lock(&player->mutex_queue);
	player->stop = TRUE;
	pthread_cond_broadcast(&player->cond_queue);
	pthread_mutex_unlock(&player->mutex_queue);
}

void player_play_prepare(struct Player *player) {
	LOGI(3, "player_set_data_source 16");
	pthread_mutex_lock(&player->mutex_queue);
	player->stop = FALSE;
	player->seek_position = DO_NOT_SEEK;
	player_assign_to_no_boolean_array(player, player->flush_streams, FALSE);
	player_assign_to_no_boolean_array(player, player->stop_streams, FALSE);

	pthread_cond_broadcast(&player->cond_queue);
	pthread_mutex_unlock(&player->mutex_queue);
}

#ifdef SUBTITLES

void player_prepare_ass_decoder_free(struct Player *player) {
	if (player->ass_track != NULL) {
		ass_free_track(player->ass_track);
	}
	if (player->ass_renderer != NULL) {
		ass_renderer_done(player->ass_renderer);
	}
	if (player->ass_library != NULL) {
		ass_library_done(player->ass_library);
	}
}

int player_prepare_ass_decoder(struct Player* player, const char *font_path) {
	AVCodecContext* ctx = player->input_codec_ctxs[player->video_stream_no];
	player->ass_library = ass_library_init();
	if (player->ass_library == NULL)
		return -ERROR_COULD_NOT_INIT_ASS_LIBRARY;

	ass_set_message_cb(player->ass_library, ass_msg_callback, NULL);

	player->ass_renderer = ass_renderer_init(player->ass_library);
	if (player->ass_renderer == NULL)
		return -ERROR_COULD_NOT_PREPARE_ASS_RENDERER;

	ass_set_frame_size(player->ass_renderer, ctx->width, ctx->height);
	LOGI(3,
			"player_prepare_ass_decoder: setting ass default font to: %s", font_path);
	ass_set_fonts(player->ass_renderer, font_path, NULL, 1, NULL, 1);
	player->ass_track = ass_new_track(player->ass_library);
	if (player->ass_track == NULL)
		return -ERROR_COULD_NOT_PREAPARE_ASS_TRACK;

	LOGI(3, "player_prepare_ass_decoder #4");

	AVCodecContext* sub_ctx =
			player->input_codec_ctxs[player->subtitle_stream_no];

	LOGI(3, "player_prepare_ass_decoder #5");
	if (sub_ctx->subtitle_header_size > 13 && sub_ctx->subtitle_header != NULL) {
		LOGI(3, "player_prepare_ass_decoder #6");
		if (!strncasecmp(sub_ctx->subtitle_header, "[Script Info]", 13)) {
			LOGI(3, "player_prepare_ass_decoder #7");
			ass_process_data(player->ass_track, sub_ctx->subtitle_header,
					sub_ctx->subtitle_header_size);
		}
	}

	return ERROR_NO_ERROR;
}

#endif // SUBTITLES


int player_alloc_video_frames(struct Player *player) {
	player->rgb_frame = avcodec_alloc_frame();
	if (player->rgb_frame == NULL) {
		LOGE(1, "player_alloc_video_frames could not allocate rgb_frame");
		return -1;
	}

	player->tmp_frame = avcodec_alloc_frame();
	if (player->tmp_frame == NULL) {
		LOGE(1, "player_alloc_video_frames could not allocate tmp_frame");
		return -1;
	}
	player->tmp_frame2 = avcodec_alloc_frame();
	if (player->tmp_frame2 == NULL) {
		LOGE(1, "player_alloc_video_frames could not allocate tmp_frame2");
		return -1;
	}
	AVCodecContext * ctx = player->input_codec_ctxs[player->video_stream_no];
	int numBytes = avpicture_get_size(PIX_FMT_RGBA, ctx->width, ctx->height);
	player->tmp_buffer = (uint8_t *) av_malloc(numBytes * sizeof(uint8_t));
	if (player->tmp_buffer == NULL) {
		LOGE(1, "player_alloc_video_frames could not allocate tmp_buffer");
		return -1;
	}
	player->tmp_buffer2 = (uint8_t *) av_malloc(numBytes * sizeof(uint8_t));
	if (player->tmp_buffer2 == NULL) {
		LOGE(1, "player_alloc_video_frames could not allocate tmp_buffer2");
		return -1;
	}
	avpicture_fill((AVPicture *) player->tmp_frame, player->tmp_buffer,
			PIX_FMT_RGBA, ctx->width, ctx->height);
	avpicture_fill((AVPicture *) player->tmp_frame2, player->tmp_buffer2,
			PIX_FMT_RGBA, ctx->width, ctx->height);
	LOGI(3, "Allocating: %dx%d", ctx->width, ctx->height);
	return 0;
}

void player_alloc_video_frames_free(struct Player *player) {
	if (player->rgb_frame != NULL) {
		avcodec_free_frame(&player->rgb_frame);
		player->rgb_frame = NULL;
	}
	if (player->tmp_frame != NULL) {
		avcodec_free_frame(&player->tmp_frame);
		player->tmp_frame = NULL;
	}
	if (player->tmp_frame2 != NULL) {
		avcodec_free_frame(&player->tmp_frame2);
		player->tmp_frame2 = NULL;
	}
	if (player->tmp_buffer != NULL) {
		av_free(player->tmp_buffer);
		player->tmp_buffer = NULL;
	}
	if (player->tmp_buffer2 != NULL) {
		av_free(player->tmp_buffer2);
		player->tmp_buffer2 = NULL;
	}
}

void player_stop_without_lock(struct State * state) {
	int ret;
	struct Player *player = state->player;

	if (!player->playing)
		return;
	player->playing = FALSE;

	LOGI(7, "player_stop_without_lock stopping...");

	player_play_prepare_free(player);
	player_start_decoding_threads_free(player);
	player_create_audio_track_free(player, state);
#ifdef SUBTITLES
	player_prepare_subtitles_queue_free(state);
#endif // SUBTITLES
	player_alloc_queues_free(state);
	player_sws_context_free(player);
	player_alloc_frames_free(player);
	player_alloc_video_frames_free(player);
#ifdef SUBTITLES
	player_prepare_ass_decoder_free(player);
#endif // SUBTITLES
	player_print_report_video_streams_free(state->env, player->thiz, player);
	player_find_streams_free(player);
	player_find_stream_info_free(player);
	player_open_input_free(player);
	player_create_context_free(player);
	LOGI(7, "player_stop_without_lock stopped");
}

void player_stop(struct State * state) {
	int ret;

	pthread_mutex_lock(&state->player->mutex_operation);
	player_stop_without_lock(state);
	pthread_mutex_unlock(&state->player->mutex_operation);
}

int player_set_data_source(struct State *state, const char *file_path,
		AVDictionary *dictionary, int video_stream_no, int audio_stream_no,
		int subtitle_stream_no) {
	struct Player *player = state->player;
	int err = ERROR_NO_ERROR;
	int i;

	pthread_mutex_lock(&player->mutex_operation);

	player_stop_without_lock(state);

	if (player->playing)
		goto end;

#ifdef SUBTITLES
	char *font_path = NULL;
	AVDictionaryEntry *entry = av_dict_get(dictionary, "ass_default_font_path",
			NULL, 0);
	if (entry != NULL) {
		LOGI(3, "ass font found: %s", entry->value);
		int length = strlen(entry->value);
		font_path = malloc(sizeof(char) * (length + 1));
		if (font_path == NULL) {
			err = ERROR_COULD_NOT_ALLOCATE_MEMORY;
			goto error;
		}
		strcpy(font_path, entry->value);
		font_path[length] = '\0';
	}
#endif // SUBTITLES
	// initial setup
	player->pause = TRUE;
	player->start_time = 0;
	player->pause_time = 0;

	// trying decode video
	if ((err = player_create_context(player)) < 0)
		goto error;

	if ((err = player_open_input(player, file_path, dictionary)) < 0)
		goto error;

	if ((err = player_find_stream_info(player)) < 0)
		goto error;

	player_print_video_informations(player, file_path);

	if ((err = player_print_report_video_streams(state->env, player->thiz,
			player)) < 0)
		goto error;

	if ((player->video_stream_no = player_find_stream(player,
			AVMEDIA_TYPE_VIDEO, video_stream_no)) < 0) {
		err = player->video_stream_no;
		goto error;
	}

	if ((player->audio_stream_no = player_find_stream(player,
			AVMEDIA_TYPE_AUDIO, audio_stream_no)) < 0) {
		err = player->audio_stream_no;
		goto error;
	}
#ifdef SUBTITLES
	if (subtitle_stream_no == NO_STREAM) {
		player->subtitle_stream_no = -1;
	} else {
		if ((player->subtitle_stream_no = player_find_stream(player,
				AVMEDIA_TYPE_SUBTITLE, subtitle_stream_no)) < 0) {
			// if no subtitles - just go without it
		}
	}

	if ((player->subtitle_stream_no >= 0)) {
		err = player_prepare_ass_decoder(player, font_path);
		if (err < 0)
			goto error;
	}
#endif // SUBTITLES
	if ((err = player_alloc_video_frames(player)) < 0) {
		goto error;
	}

	if ((err = player_alloc_frames(player)) < 0)
		goto error;

	if ((err = player_alloc_queues(state)) < 0)
		goto error;

	struct DecoderState video_decoder_state = { stream_no
			: player->video_stream_no, player: player, env:state->env};
#ifdef SUBTITLES
	if (player->subtitle_stream_no >= 0) {
		struct DecoderState subtitle_decoder_state = { stream_no
				: player->subtitle_stream_no, player: player, env:state->env};
		if ((err = player_prepare_subtitles_queue(&subtitle_decoder_state,
				state)) < 0)
			goto error;
	}
#endif // SUBTITLES
	if ((err = player_create_audio_track(player, state)) < 0)
		goto error;

	player_get_video_duration(player);
	player_update_time(state, FALSE);

	player_play_prepare(player);

	if ((err = player_start_decoding_threads(player)) < 0) {
		goto error;
	}

	// SUCCESS
	player->playing = TRUE;
	LOGI(3, "player_set_data_source success");
	goto end;

	error:
	LOGI(3, "player_set_data_source error");

	player_play_prepare_free(player);
	player_start_decoding_threads_free(player);
	player_create_audio_track_free(player, state);
#ifdef SUBTITLES
	player_prepare_subtitles_queue_free(state);
#endif // SUBTITLES
	player_alloc_queues_free(state);
	player_alloc_frames_free(player);
	player_alloc_video_frames_free(player);
#ifdef SUBTITLES
	player_prepare_ass_decoder_free(player);
#endif // SUBTITLES
	player_print_report_video_streams_free(state->env, player->thiz, player);
	player_find_streams_free(player);
	player_find_stream_info_free(player);
	player_open_input_free(player);
	player_create_context_free(player);
#ifdef SUBTITLES
	if (font_path != NULL)
		free(font_path);
#endif // SUBTITLES
	end:
	LOGI(7, "player_set_data_source end");
	pthread_mutex_unlock(&player->mutex_operation);
	return err;
}

int player_get_next_frame(int current_frame, int max_frame) {
	return (current_frame + 1) % max_frame;
}

void jni_player_seek(JNIEnv *env, jobject thiz, jlong positionUs) {
	struct Player *player = player_get_player_field(env, thiz);
	pthread_mutex_lock(&player->mutex_operation);

	if (!player->playing) {
		LOGI(1, "jni_player_seek could not seek while not playing");
		throw_exception(env, not_playing_exception_class_path_name,
				"Could not pause while not playing");
		goto end;
	}
	pthread_mutex_lock(&player->mutex_queue);
	player->seek_position = positionUs;
	pthread_cond_broadcast(&player->cond_queue);

	while (player->seek_position != DO_NOT_SEEK)
		pthread_cond_wait(&player->cond_queue, &player->mutex_queue);
	pthread_mutex_unlock(&player->mutex_queue);
	end: pthread_mutex_unlock(&player->mutex_operation);
}

void jni_player_pause(JNIEnv *env, jobject thiz) {
	struct Player * player = player_get_player_field(env, thiz);

	pthread_mutex_lock(&player->mutex_operation);

	if (!player->playing) {
		LOGI(1, "jni_player_pause could not pause while not playing");
		throw_exception(env, not_playing_exception_class_path_name,
				"Could not pause while not playing");
		goto end;
	}

	pthread_mutex_lock(&player->mutex_queue);

	if (player->pause)
		goto do_nothing;

	LOGI(3, "jni_player_pause Pausing");
	player->pause = TRUE;
	player->pause_time = av_gettime();
	pthread_cond_broadcast(&player->cond_queue);

	(*env)->CallVoidMethod(env, player->audio_track,
			player->audio_track_pause_method);
	// just leave exception

do_nothing:
	pthread_mutex_unlock(&player->mutex_queue);

end:
	pthread_mutex_unlock(&player->mutex_operation);

}

void jni_player_resume(JNIEnv *env, jobject thiz) {
	struct Player * player = player_get_player_field(env, thiz);
	pthread_mutex_lock(&player->mutex_operation);

	if (!player->playing) {
		LOGI(1, "jni_player_resume could not pause while not playing");
		throw_exception(env, not_playing_exception_class_path_name,
				"Could not resume while not playing");
		goto end;
	}

	pthread_mutex_lock(&player->mutex_queue);

	if (!player->pause)
		goto do_nothing;

	player->pause = FALSE;

	int64_t resume_time = av_gettime();
	player->start_time += resume_time - player->pause_time;

	pthread_cond_broadcast(&player->cond_queue);

	(*env)->CallVoidMethod(env, player->audio_track,
			player->audio_track_play_method);
	// just leave exception

do_nothing:
	pthread_mutex_unlock(&player->mutex_queue);

end:
	pthread_mutex_unlock(&player->mutex_operation);
}

void jni_player_read_dictionary(JNIEnv *env, AVDictionary **dictionary,
		jobject jdictionary) {
	jclass map_class = (*env)->FindClass(env, map_class_path_name);
	jclass set_class = (*env)->FindClass(env, set_class_path_name);
	jclass iterator_class = (*env)->FindClass(env, iterator_class_path_name);

	jmethodID map_key_set_method = java_get_method(env, map_class, map_key_set);
	jmethodID map_get_method = java_get_method(env, map_class, map_get);

	jmethodID set_iterator_method = java_get_method(env, set_class,
			set_iterator);

	jmethodID iterator_next_method = java_get_method(env, iterator_class,
			iterator_next);
	jmethodID iterator_has_next_method = java_get_method(env, iterator_class,
			iterator_has_next);

	jobject jkey_set = (*env)->CallObjectMethod(env, jdictionary,
			map_key_set_method);
	jobject jiterator = (*env)->CallObjectMethod(env, jkey_set,
			set_iterator_method);

	while ((*env)->CallBooleanMethod(env, jiterator, iterator_has_next_method)) {
		jobject jkey = (*env)->CallObjectMethod(env, jiterator,
				iterator_next_method);
		jobject jvalue = (*env)->CallObjectMethod(env, jdictionary,
				map_get_method, jkey);

		const char *key = (*env)->GetStringUTFChars(env, jkey, NULL);
		const char *value = (*env)->GetStringUTFChars(env, jvalue, NULL);

		if (av_dict_set(dictionary, key, value, 0) < 0) {
			LOGE(2, "player_set_data_source: could not set key");
		}

		(*env)->ReleaseStringUTFChars(env, jkey, key);
		(*env)->ReleaseStringUTFChars(env, jvalue, value);
		(*env)->DeleteLocalRef(env, jkey);
		(*env)->DeleteLocalRef(env, jvalue);
	}

	(*env)->DeleteLocalRef(env, jiterator);
	(*env)->DeleteLocalRef(env, jkey_set);

	(*env)->DeleteLocalRef(env, map_class);
	(*env)->DeleteLocalRef(env, set_class);
	(*env)->DeleteLocalRef(env, iterator_class);
}

int jni_player_set_data_source(JNIEnv *env, jobject thiz, jstring string,
		jobject dictionary, int video_stream_no, int audio_stream_no,
		int subtitle_stream_no) {

	AVDictionary *dict = NULL;
	if (dictionary != NULL) {
		jni_player_read_dictionary(env, &dict, dictionary);
		(*env)->DeleteLocalRef(env, dictionary);
	}

	const char *file_path = (*env)->GetStringUTFChars(env, string, NULL);
	struct Player * player = player_get_player_field(env, thiz);
	struct State state = { player: player, env: env};

	int ret = player_set_data_source(&state, file_path, dict, video_stream_no,
			audio_stream_no, subtitle_stream_no);

	(*env)->ReleaseStringUTFChars(env, string, file_path);
	return ret;
}

void jni_player_dealloc(JNIEnv *env, jobject thiz) {
	struct Player *player = player_get_player_field(env, thiz);
	if (player->thiz != NULL) {
		(*env)->DeleteGlobalRef(env, player->thiz);
	}
	if (player->audio_track_class != NULL) {
		(*env)->DeleteGlobalRef(env, player->audio_track_class);
	}
	free(player);
}

int jni_player_init(JNIEnv *env, jobject thiz) {

#ifdef PROFILER
#warning "Profiler enabled"
	setenv("CPUPROFILE_FREQUENCY", "1000", 1);
	monstartup("libffmpeg.so");
#endif

	struct Player *player = malloc(sizeof(struct Player));
	memset(player, 0, sizeof(player));
	player->audio_stream_no = -1;
	player->video_stream_no = -1;
#ifdef SUBTITLES
	player->subtitle_stream_no = -1;
#endif // SUBTITLES
	int err = ERROR_NO_ERROR;

	int ret = (*env)->GetJavaVM(env, &player->get_javavm);
	if (ret) {
		err = ERROR_COULD_NOT_GET_JAVA_VM;
		goto free_player;
	}

	{
		player->thiz = (*env)->NewGlobalRef(env, thiz);
		if (player->thiz == NULL) {
			err = ERROR_COULD_NOT_CREATE_GLOBAL_REF;
			goto free_player;
		}
	}

	{
		jclass player_class = (*env)->FindClass(env, player_class_path_name);

		if (player_class == NULL) {
			err = ERROR_NOT_FOUND_PLAYER_CLASS;
			goto free_player;
		}

		jfieldID player_m_native_player_field = java_get_field(env,
				player_class_path_name, player_m_native_player);
		if (player_m_native_player_field == NULL) {
			err = ERROR_NOT_FOUND_M_NATIVE_PLAYER_FIELD;
			goto free_player;
		}

		(*env)->SetIntField(env, thiz, player_m_native_player_field,
				(jint) player);

		player->player_prepare_frame_method = java_get_method(env, player_class,
				player_prepare_frame);
		if (player->player_prepare_frame_method == NULL) {
			err = ERROR_NOT_FOUND_PREPARE_FRAME_METHOD;
			goto free_player;
		}

		player->player_on_update_time_method = java_get_method(env,
				player_class, player_on_update_time);
		if (player->player_on_update_time_method == NULL) {
			err = ERROR_NOT_FOUND_ON_UPDATE_TIME_METHOD;
			goto free_player;
		}

		player->player_prepare_audio_track_method = java_get_method(env,
				player_class, player_prepare_audio_track);
		if (player->player_prepare_audio_track_method == NULL) {
			err = ERROR_NOT_FOUND_PREPARE_AUDIO_TRACK_METHOD;
			goto free_player;
		}

		player->player_set_stream_info_method = java_get_method(env,
				player_class, player_set_stream_info);
		if (player->player_set_stream_info_method == NULL) {
			err = ERROR_NOT_FOUND_SET_STREAM_INFO_METHOD;
			goto free_player;
		}

		(*env)->DeleteLocalRef(env, player_class);
	}

	{
		jclass audio_track_class = (*env)->FindClass(env,
				android_track_class_path_name);
		if (audio_track_class == NULL) {
			err = ERROR_NOT_FOUND_AUDIO_TRACK_CLASS;
			goto free_player;
		}

		player->audio_track_class = (*env)->NewGlobalRef(env,
				audio_track_class);
		if (player->audio_track_class == NULL) {
			err = ERROR_COULD_NOT_CREATE_GLOBAL_REF_FOR_AUDIO_TRACK_CLASS;
			(*env)->DeleteLocalRef(env, audio_track_class);
			goto free_player;
		}
		(*env)->DeleteLocalRef(env, audio_track_class);
	}

	player->audio_track_write_method = java_get_method(env,
			player->audio_track_class, audio_track_write);
	if (player->audio_track_write_method == NULL) {
		err = ERROR_NOT_FOUND_WRITE_METHOD;
		goto delete_audio_track_global_ref;
	}

	player->audio_track_play_method = java_get_method(env,
			player->audio_track_class, audio_track_play);
	if (player->audio_track_play_method == NULL) {
		err = ERROR_NOT_FOUND_PLAY_METHOD;
		goto delete_audio_track_global_ref;
	}

	player->audio_track_pause_method = java_get_method(env,
			player->audio_track_class, audio_track_pause);
	if (player->audio_track_pause_method == NULL) {
		err = ERROR_NOT_FOUND_PAUSE_METHOD;
		goto delete_audio_track_global_ref;
	}

	player->audio_track_flush_method = java_get_method(env,
			player->audio_track_class, audio_track_flush);
	if (player->audio_track_flush_method == NULL) {
		err = ERROR_NOT_FOUND_FLUSH_METHOD;
		goto delete_audio_track_global_ref;
	}

	player->audio_track_stop_method = java_get_method(env,
			player->audio_track_class, audio_track_stop);
	if (player->audio_track_stop_method == NULL) {
		err = ERROR_NOT_FOUND_STOP_METHOD;
		goto delete_audio_track_global_ref;
	}

	player->audio_track_get_channel_count_method = java_get_method(env,
			player->audio_track_class, audio_track_get_channel_count);
	if (player->audio_track_get_channel_count_method == NULL) {
		err = ERROR_NOT_FOUND_GET_CHANNEL_COUNT_METHOD;
		goto delete_audio_track_global_ref;
	}

	player->audio_track_get_sample_rate_method = java_get_method(env,
			player->audio_track_class, audio_track_get_sample_rate);
	if (player->audio_track_get_sample_rate_method == NULL) {
		err = ERROR_NOT_FOUND_GET_SAMPLE_RATE_METHOD;
		goto delete_audio_track_global_ref;
	}

	pthread_mutex_init(&player->mutex_operation, NULL);
	pthread_mutex_init(&player->mutex_queue, NULL);
#ifdef SUBTITLES
	pthread_mutex_init(&player->mutex_ass, NULL);
#endif // SUBTITLES
	pthread_cond_init(&player->cond_queue, NULL);

	player->playing = FALSE;
	player->pause = FALSE;
	player->stop = TRUE;
	player->flush_video_play = FALSE;

	av_log_set_callback(ffmpeg_log_callback);
	av_log_set_level(FFMPEG_LOG_LEVEL);
	avformat_network_init();
	av_register_all();
	register_jni_protocol(player->get_javavm);
#ifdef MODULE_ENCRYPT
	register_aes_protocol();
#endif

	player_print_all_codecs();

	goto end;

	delete_audio_track_global_ref: (*env)->DeleteGlobalRef(env,
			player->audio_track_class);

free_player:
	if (player->thiz != NULL) {
		(*env)->DeleteGlobalRef(env, player->thiz);
	}
	if (player->audio_track_class != NULL) {
		(*env)->DeleteGlobalRef(env, player->audio_track_class);
	}
	free(player);

end:
	return err;
}

void jni_player_stop(JNIEnv *env, jobject thiz) {
#ifdef PROFILER
	moncleanup();
#endif

	struct Player * player = player_get_player_field(env, thiz);
	struct State state = {player: player, env: env};
	player_stop(&state);
}

jlong jni_player_get_video_duration(JNIEnv *env, jobject thiz) {
	struct Player * player = player_get_player_field(env, thiz);
	return player->video_duration;
}

void jni_player_render(JNIEnv *env, jobject thiz, jobject surface) {
	struct Player * player = player_get_player_field(env, thiz);
	ANativeWindow* window = ANativeWindow_fromSurface(env, surface);

	LOGI(4, "jni_player_render")
	pthread_mutex_lock(&player->mutex_queue);
	if (player->window != NULL) {
		LOGE(1,
				"jni_player_render Window have to be null before "
				"calling render function");
		exit(1);
	}
	ANativeWindow_acquire(window);
	player->window = window;
	pthread_cond_broadcast(&player->cond_queue);
	pthread_mutex_unlock(&player->mutex_queue);
}

void jni_player_render_frame_start(JNIEnv *env, jobject thiz) {

}

void jni_player_render_frame_stop(JNIEnv *env, jobject thiz) {
	struct Player * player = player_get_player_field(env, thiz);

	struct State state = {player: player, env: env};

	LOGI(5, "jni_player_render_frame_stop stopping render");

	player_stop(&state);

	LOGI(5, "jni_player_render_frame_stop waiting for mutex");
	pthread_mutex_lock(&player->mutex_queue);
	if (player->window == NULL) {
		LOGE(1,
				"jni_player_render_frame_stop Window is null this "
				"mean that you did not call render function");
		exit(1);
	}
	LOGI(5, "jni_player_render_frame_stop releasing window");
	ANativeWindow_release(player->window);
	player->window = NULL;
	pthread_cond_broadcast(&player->cond_queue);
	pthread_mutex_unlock(&player->mutex_queue);
}

