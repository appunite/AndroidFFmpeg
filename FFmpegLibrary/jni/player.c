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

#include <jni.h>
#include <pthread.h>

/* Android profiler */
#ifdef PROFILER
#include <android-ndk-profiler-3.1/prof.h>
#endif

#ifdef YUV2RGB
#include <yuv2rgb/yuv2rgb.h>
#endif

/*local headers*/

#include "helpers.h"
#include "queue.h"
#include "player.h"
#include "jni-protocol.h"
#include "aes-protocol.h"

#define LOG_LEVEL 15
#define LOG_TAG "player.c"
#define LOGI(level, ...) if (level <= LOG_LEVEL) {__android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__);}
#define LOGE(level, ...) if (level <= LOG_LEVEL) {__android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__);}

#define FALSE 0
#define TRUE (!(FALSE))


#define DO_NOT_SEEK -1

// 1000000 us = 1 s
#define MIN_SLEEP_TIME_US 10000
// 10000 ms = 1s
#define MIN_SLEEP_TIME_MS 2

void player_print_codec_description(AVCodec *codec) {
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
			"player_print_codec_description id: %d codec: %s, type: %s", codec->id, codec->name, type);
}

struct VideoRGBFrameElem {

	AVFrame *frame;
	jobject jbitmap;
	double time;
};

struct SubtitleElem {
	AVSubtitle subtitle;
	double start_time;
	double stop_time;
};

#define MAX_STREAMS 3

struct Player {
	JavaVM *get_javavm;

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

	pthread_mutex_t mutex_operation;

	int caputre_streams_no;

	int video_stream_no;
	int audio_stream_no;

	AVStream *input_streams[MAX_STREAMS];
	AVCodecContext * input_codec_ctxs[MAX_STREAMS];
	int input_stream_numbers[MAX_STREAMS];
	AVCodec *input_codecs[MAX_STREAMS];
	AVFrame *input_frames[MAX_STREAMS];

	AVFormatContext *input_format_ctx;
	int input_inited;


	enum PixelFormat out_format;


	jobject audio_track;
	enum AVSampleFormat audio_track_format;
	int audio_track_channel_count;

	struct SwsContext *sws_context;
	struct SwrContext *swr_context;
	DECLARE_ALIGNED(16,uint8_t,audio_buf2)[AVCODEC_MAX_AUDIO_FRAME_SIZE * 4];

	long video_duration;
	int last_updated_time;

	//	pthread_mutex_t ready_mutex;
	//	pthread_cond_t ready_cond;
	//	int is_ready;

	int playing;

	pthread_mutex_t mutex_queue;
	pthread_cond_t cond_queue;
	Queue *packets[MAX_STREAMS];
	Queue *rgb_video_frames;
	Queue *subtitles_queue;

	int interrupt_renderer;
	int pause;
	int stop;
	int seek_position;
//	int flush_audio_stream;
//	int flush_video_stream;
	int flush_streams[MAX_STREAMS];
	int flush_video_play;


//	int stop_audio_stream;
//	int stop_video_stream;
	int stop_streams[MAX_STREAMS];

	int rendering;

	pthread_t thread_player_read_from_stream;
	pthread_t decode_threads[MAX_STREAMS];


	int thread_player_read_from_stream_created;
	int decode_threads_created[MAX_STREAMS];

	double audio_clock;
	int64_t audio_write_time;

	int64_t audio_pause_time;
	int64_t audio_resume_time;

#ifdef YUV2RGB
int dither;
#endif
};

struct State {
	struct Player *player;
	JNIEnv* env;
	jobject thiz;
};

struct DecoderState {
	int stream_no;
	struct Player *player;
	JNIEnv* env;
	jobject thiz;
};

struct DecoderData {
	struct Player *player;
	int stream_no;
};

enum Msgs {
	MSG_NONE = 0, MSG_STOP = 1
};

enum PlayerErrors {
	ERROR_NO_ERROR = 0,

	// Java errors
	ERROR_NOT_FOUND_PLAYER_CLASS,
	ERROR_NOT_FOUND_PREPARE_FRAME_METHOD,
	ERROR_NOT_FOUND_ON_UPDATE_TIME_METHOD,
	ERROR_NOT_FOUND_PREPARE_AUDIO_TRACK_METHOD,
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
};

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
int player_decode_audio(struct DecoderData * decoder_data, JNIEnv * env, AVPacket *packet) {
	int got_frame_ptr;
	struct Player *player = decoder_data->player;
	int stream_no = decoder_data->stream_no;
	AVCodecContext * ctx = player->input_codec_ctxs[stream_no];
	AVFrame * frame = player->input_frames[stream_no];

	LOGI(3, "player_decode_audio decoding");
	int len = avcodec_decode_audio4(ctx,
			frame, &got_frame_ptr, packet);

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
		if (len2
				== sizeof(player->audio_buf2) / sample_per_buffer_divider) {
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
	if (!player->rendering) {
		LOGI(2,
				"player_decode_video not rendering flushing rgb_video_frames");
		struct VideoRGBFrameElem *elem;
		while ((elem = queue_pop_start_already_locked_non_block(
				player->rgb_video_frames)) != NULL) {
			queue_pop_finish_already_locked(player->rgb_video_frames);
		}
	} else {
		LOGI(2,
				"player_decode_video rendering sending rgb_video_frames flush request");
		player->flush_video_play = TRUE;
		pthread_cond_broadcast(&player->cond_queue);
		LOGI(2, "player_decode_video waiting for rgb_video_frames flush");
		while (player->flush_video_play)
			pthread_cond_wait(&player->cond_queue, &player->mutex_queue);
	}
}

int player_decode_video(struct DecoderData * decoder_data, JNIEnv * env, AVPacket *packet) {
	int got_frame_ptr;
	struct Player *player = decoder_data->player;
	int stream_no = decoder_data->stream_no;
	AVCodecContext * ctx = player->input_codec_ctxs[stream_no];
	AVFrame * frame = player->input_frames[stream_no];
	AVStream * stream = player->input_streams[stream_no];
	int interrupt_ret;

	LOGI(10, "player_decode_video decoding");
	int frameFinished;
	int ret = avcodec_decode_video2(ctx,
			frame, &frameFinished, packet);

	if (ret < 0) {
		LOGE(1, "player_decode_video Fail decoding video %d\n", ret);
		return -ERROR_WHILE_DECODING_VIDEO;
	}
	if (!frameFinished) {
		LOGI(10, "player_decode_video Video frame not finished\n");
		return 0;
	}

	int64_t pts = av_frame_get_best_effort_timestamp(frame);
	if (pts == AV_NOPTS_VALUE) {
		pts = 0;
	}

	double time = (double) pts
			* av_q2d(stream->time_base);
	LOGI(10,
			"player_decode_video Decoded video frame: %f, time_base: %d", time, pts);

	// saving in buffer converted video frame
	LOGI(7, "player_decode_video copy wait");
	int to_write;
	struct VideoRGBFrameElem * elem;

	pthread_mutex_lock(&player->mutex_queue);
	push: elem = queue_push_start_already_locked(player->rgb_video_frames,
			&to_write,
			(QueueCheckFunc) player_decode_queue_check_func, decoder_data,
			(void **) &interrupt_ret);
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
	elem->time = time;
	AVFrame * rgbFrame = elem->frame;
	void *buffer;
	int destWidth = ctx->width;
	int destHeight = ctx->height;
	int err = 0;

	if ((ret = AndroidBitmap_lockPixels(env, elem->jbitmap, &buffer)) < 0) {
		LOGE(1, "AndroidBitmap_lockPixels() failed ! error=%d", ret);
		err = -ERROR_WHILE_LOCING_BITMAP;
		goto fail_lock_bitmap;
	}

	avpicture_fill((AVPicture *) elem->frame, buffer, player->out_format,
			destWidth, destHeight);

	LOGI(7, "player_decode_video copying...");
#ifdef YUV2RGB
	yuv420_2_rgb565(rgbFrame->data[0], frame->data[0], frame->data[1],
			frame->data[2], destWidth, destHeight, frame->linesize[0],
			frame->linesize[1], destWidth << 1, yuv2rgb565_table,
			player->dither++);
#else
	sws_scale(player->sws_context,
			(const uint8_t * const *) frame->data,
			frame->linesize, 0, ctx->height,
			rgbFrame->data, rgbFrame->linesize);
#endif


	AndroidBitmap_unlockPixels(env, elem->jbitmap);

	fail_lock_bitmap:
	queue_push_finish(player->rgb_video_frames, to_write);
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

	JavaVMAttachArgs thread_spec =
			{ JNI_VERSION_1_4, thread_title, NULL };

	jint ret = (*player->get_javavm)->AttachCurrentThread(player->get_javavm,
			&env, &thread_spec);
	if (ret || env == NULL) {
		err = -ERROR_COULD_NOT_ATTACH_THREAD;
		goto end;
	}

	for (;;) {
		LOGI(10, "player_decode waiting for frame[%d]", stream_no);
		int interrupt_ret;
		AVPacket *packet;
		pthread_mutex_lock(&player->mutex_queue);
		pop: packet = queue_pop_start_already_locked(queue,
				(QueueCheckFunc) player_decode_queue_check_func, decoder_data,
				(void **) &interrupt_ret);

		if (packet == NULL) {
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

		if (codec_type == AVMEDIA_TYPE_AUDIO) {
			err = player_decode_audio(decoder_data, env, packet);
		} else if (codec_type == AVMEDIA_TYPE_VIDEO) {
			err = player_decode_video(decoder_data, env, packet);
		} else {
			assert(FALSE);
		}

		av_free_packet(packet);
		queue_pop_finish(queue);
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
		AVPacket *to_free;
		while ((to_free = queue_pop_start_already_locked_non_block(
				queue)) != NULL) {
			av_free_packet(to_free);
			queue_pop_finish_already_locked(queue);
		}
		LOGI(2, "player_decode flushing playback[%d]", stream_no);

		if (codec_type == AVMEDIA_TYPE_AUDIO) {
			player_decode_audio_flush(decoder_data, env);
		} else if (codec_type == AVMEDIA_TYPE_VIDEO) {
			player_decode_video_flush(decoder_data, env);
		} else {
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

	end:
	free(decoder_data);
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

static void player_assign_to_no_boolean_array(struct Player *player, int* array, int value) {
	int capture_streams_no = player->caputre_streams_no;
	int stream_no;
	for (stream_no = 0; stream_no < capture_streams_no; ++stream_no) {
		array[stream_no] = value;
	}
}
static int player_if_all_no_array_elements_has_value(struct Player *player, int *array, int value) {
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

		parse_frame:
			queue = NULL;
			LOGI(3, "player_read_from_stream looking for stream")
			for (stream_no = 0; stream_no < caputre_streams_no; ++stream_no) {
				if (packet.stream_index == player->input_stream_numbers[stream_no]) {
					queue = player->packets[stream_no];
					LOGI(3, "player_read_from_stream stream found [%d]", stream_no);
				}
			}

		if (queue == NULL) {
			LOGI(3, "player_read_from_stream stream not found");
			goto skip_loop;
		}

		AVPacket * new_packet;
		int to_write;
		int interrupt_ret;

		push_start:
		LOGI(10, "player_read_from_stream waiting for queue");
		new_packet = queue_push_start_already_locked(queue, &to_write,
				(QueueCheckFunc) player_read_from_stream_check_func, player,
				(void **) &interrupt_ret);
		if (new_packet == NULL) {
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

		*new_packet = packet;

		if (av_dup_packet(new_packet) < 0) {
			err = ERROR_WHILE_DUPLICATING_FRAME;
			pthread_mutex_lock(&player->mutex_queue);
			goto exit_loop;
		}

		queue_push_finish(queue, to_write);

		goto end_loop;

		exit_loop:
		LOGI(3, "player_read_from_stream stop");
		av_free_packet(pkt);

		//request stream to stop
		player_assign_to_no_boolean_array(player, player->stop_streams, TRUE);
		pthread_cond_broadcast(&player->cond_queue);

		// wait for all stream stop
		while (!player_if_all_no_array_elements_has_value(player, player->stop_streams, FALSE))
			pthread_cond_wait(&player->cond_queue, &player->mutex_queue);

		// flush internal buffers
		for (stream_no = 0; stream_no < caputre_streams_no; ++stream_no) {
			avcodec_flush_buffers(player->input_codec_ctxs[stream_no]);
		}

		pthread_mutex_unlock(&player->mutex_queue);
		goto detach_current_thread;

		seek_loop:
		// setting stream thet will be used as a base for seeking
		seek_input_stream_number = player->input_stream_numbers[player->video_stream_no];
		seek_input_stream = player->input_streams[player->video_stream_no];

		// getting seek target time in time_base value
		seek_target = av_rescale_q(
				AV_TIME_BASE * (int64_t) player->seek_position, AV_TIME_BASE_Q,
				seek_input_stream->time_base);
		LOGI(3, "player_read_from_stream seeking to: "
		"%ds, time_base: %d", player->seek_position, seek_target);

		// seeking
		if (av_seek_frame(player->input_format_ctx,
				seek_input_stream_number, seek_target, 0) < 0) {
			// seeking error - trying to play movie without it
			LOGE(1, "Error while seeking");
			player->seek_position = DO_NOT_SEEK;
			pthread_cond_broadcast(&player->cond_queue);
			goto parse_frame;
		}

		LOGI(3, "player_read_from_stream seeking success");

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
		while (!player_if_all_no_array_elements_has_value(player, player->flush_streams, FALSE))
				pthread_cond_wait(&player->cond_queue, &player->mutex_queue);

		LOGI(3, "player_read_from_stream flushing internal codec bffers");
		// flush internal buffers
		for (stream_no = 0; stream_no < caputre_streams_no; ++stream_no) {
			avcodec_flush_buffers(player->input_codec_ctxs[stream_no]);
		}

		// finishing seeking
		player->seek_position = DO_NOT_SEEK;
		pthread_cond_broadcast(&player->cond_queue);
		LOGI(3, "player_read_from_stream ending seek");


		skip_loop:
		av_free_packet(pkt);
		pthread_mutex_unlock(&player->mutex_queue);

		end_loop:
		continue;
	}

	detach_current_thread: ret = (*player->get_javavm)->DetachCurrentThread(
			player->get_javavm);
	if (ret && !err)
		err = ERROR_COULD_NOT_DETACH_THREAD;

	end:

	// TODO do something with error valuse
	return NULL;
}

int player_write_audio(struct DecoderData *decoder_data, JNIEnv *env, int64_t pts,
		uint8_t *data, int data_size, int original_data_size) {
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

	pthread_mutex_lock(&player->mutex_queue);

	if (pts != AV_NOPTS_VALUE) {
		player->audio_clock = av_q2d(stream->time_base)
				* pts;
	} else {
		player->audio_clock += (double) original_data_size
				/ (c->channels * c->sample_rate
						* av_get_bytes_per_sample(c->sample_fmt));
	}
	player->audio_write_time = av_gettime();
	pthread_cond_broadcast(&player->cond_queue);
	pthread_mutex_unlock(&player->mutex_queue);

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

void *player_fill_packet(struct Player *player) {
	return malloc(sizeof(AVPacket));
}

void player_free_packet(struct Player *player, AVPacket *elem) {
	free(elem);
}

void *player_fill_subtitles_queue(struct DecoderState *decoder_state) {
	return malloc(sizeof(struct SubtitleElem));
}

void player_free_subtitles_queue(struct DecoderState *decoder_state, struct SubtitleElem *elem) {
	free(elem);
}

void player_free_video_rgb_frame(struct DecoderState *decoder_state,
		struct VideoRGBFrameElem *elem) {
	JNIEnv *env = decoder_state->env;
	jobject thiz = decoder_state->thiz;

	(*env)->DeleteGlobalRef(env, elem->jbitmap);
	av_free(elem->frame);
	free(elem);
}

void *player_fill_video_rgb_frame(struct DecoderState *decoder_state) {
	struct Player *player = decoder_state->player;
	JNIEnv *env = decoder_state->env;
	jobject thiz = decoder_state->thiz;
	int stream_no = decoder_state->stream_no;
	AVCodecContext * ctx = player->input_codec_ctxs[player->video_stream_no];

	struct VideoRGBFrameElem * elem = malloc(sizeof(struct VideoRGBFrameElem));

	if (elem == NULL) {
		LOGE(1,
				"player_fill_video_rgb_frame could no allocate VideoRGBFrameEelem");
		goto error;
	}

	elem->frame = avcodec_alloc_frame();
	if (elem->frame == NULL) {
		LOGE(1, "player_fill_video_rgb_frame could not create frame")
		goto free_elem;
	}

	int destWidth = ctx->width;
	int destHeight = ctx->height;

	LOGI(3,
			"player_fill_video_rgb_frame prepareFrame(%d, %d)", destWidth, destHeight);
	jobject jbitmap = (*env)->CallObjectMethod(env, thiz,
			player->player_prepare_frame_method, destWidth, destHeight);

	jthrowable exc = (*env)->ExceptionOccurred(env);
	if (exc) {
		LOGE(1,
				"player_fill_video_rgb_frame could not create jbitmap - exception occure");
		goto free_frame;
	}
	if (jbitmap == NULL) {
		LOGE(1, "player_fill_video_rgb_frame could not create jbitmap");
		goto free_frame;
	}

	elem->jbitmap = (*env)->NewGlobalRef(env, jbitmap);
	if (elem->jbitmap == NULL) {
		goto free_frame;
	}
	(*env)->DeleteLocalRef(env, jbitmap);

	goto end;

	release_ref:
	(*env)->DeleteGlobalRef(env, elem->jbitmap);
	elem->jbitmap = NULL;

	free_frame: av_free(elem->frame);
	elem->frame = NULL;

	free_elem: free(elem);
	elem = NULL;

	error: end: return elem;

}

void player_update_time(struct State *state, double time) {
	int time_int = round(time);

	struct Player *player = state->player;
	if (player->last_updated_time == time_int) {
		return;
	}
	player->last_updated_time = time_int;

	// because video duation can be estimate
	// we have to ensure that it will not be smaller
	// than current time
	if (time_int > player->video_duration)
		player->video_duration = time_int;

	(*state->env)->CallVoidMethod(state->env, state->thiz,
			player->player_on_update_time_method, time_int,
			player->video_duration);
}

void player_open_stream_free(struct Player *player, int stream_no) {
	AVCodecContext ** ctx = &player->input_codec_ctxs[stream_no];
	if (*ctx != NULL) {
		avcodec_close(*ctx);
		*ctx = NULL;
	}
}

int player_open_stream(struct Player *player, AVCodecContext * ctx, AVCodec **codec) {
	enum AVCodecID codec_id = ctx->codec_id;
	LOGI(3, "player_open_stream trying open: %d", codec_id);

	*codec = avcodec_find_decoder(
			codec_id);
	if (*codec == NULL) {
		LOGE(1,
				"player_set_data_source Could not find codec for id: %d", codec_id);
		return -ERROR_COULD_NOT_FIND_VIDEO_CODEC;
	}

	if (avcodec_open2(ctx, *codec, NULL) < 0) {
		LOGE(1, "Could not open codec");
		player_print_codec_description(*codec);
		*codec = NULL;
		return -ERROR_COULD_NOT_OPEN_VIDEO_CODEC;
	}


	LOGI(3, "player_open_stream opened: %d", codec_id);
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
}

int player_find_stream(struct Player *player, enum AVMediaType codec_type) {
	//find video stream
	int streams_no = player->caputre_streams_no;

	int bn_stream = -1;
	int stream_no;
	int err = ERROR_NO_ERROR;
	LOGI(3, "player_find_stream, type: %d", codec_type);

	for (stream_no = 0; stream_no < player->input_format_ctx->nb_streams; stream_no++) {
		AVStream *stream = player->input_format_ctx->streams[stream_no];
		AVCodecContext *codec = stream->codec;
		if (codec->codec_type == codec_type) {
			AVCodecContext * ctx = stream->codec;
			AVCodec *codec;
			err = player_open_stream(player, ctx, &codec);
			if (err < 0) {
				// could not open selected stream
				continue;
			}
			player->input_codecs[streams_no] = codec;
			bn_stream = stream_no;
			break;
		}
	}
	if (bn_stream < 0) {
		if (err < 0)
			return err;
		return -1;
	}

	LOGI(3, "player_set_data_source 4");
	player->input_streams[streams_no] = player->input_format_ctx->streams[bn_stream];
	player->input_codec_ctxs[streams_no] = player->input_streams[streams_no]->codec;
	player->input_stream_numbers[streams_no] = bn_stream;

	if (codec_type == AVMEDIA_TYPE_VIDEO) {
		LOGI(5,
			"player_set_data_source Video size is [%d x %d]",
			player->input_codec_ctxs[streams_no]->width,
			player->input_codec_ctxs[streams_no]->height);
	}

	player->caputre_streams_no += 1;
	return bn_stream;
}

uint64_t player_find_layout_from_channels(int nb_channels) {
	int i;
	for (i = 0; i < FF_ARRAY_ELEMS(channel_android_layout_map); i++)
		if (nb_channels == channel_android_layout_map[i].nb_channels)
			return channel_android_layout_map[i].layout;
	return (uint64_t)0;
}

void player_print_video_informations(struct Player *player, const char *file_path) {
	if (LOG_LEVEL >= 3) {
		int i;
		av_dump_format(player->input_format_ctx, 0, file_path, FALSE);
		LOGI(3,
				"player_set_data_source Number of streams: %d", player->input_format_ctx->nb_streams);
		for (i = 0; i < player->input_format_ctx->nb_streams; i++) {
			AVStream *stream = player->input_format_ctx->streams[i];
			AVCodecContext *codec = stream->codec;
			LOGI(3, "- stream: %d", i);
			AVDictionary *metadaat = stream->metadata;
			AVDictionaryEntry *tag = NULL;
			LOGI(3, "-- metadata:")
			while ((tag = av_dict_get(metadaat, "", tag, AV_DICT_IGNORE_SUFFIX))) {
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

int player_alloc_queues(struct Player *player) {
	int capture_streams_no = player->caputre_streams_no;
	int stream_no;
	for (stream_no = 0; stream_no < capture_streams_no; ++stream_no) {
		player->packets[stream_no] = queue_init_with_custom_lock(100,
				(queue_fill_func) player_fill_packet,
				(queue_free_func) player_free_packet, player,
				&player->mutex_queue, &player->cond_queue);
		if (player->packets[stream_no] == NULL) {
			return -ERROR_COULD_NOT_PREPARE_PACKETS_QUEUE;
		}
	}
	return 0;
}
void player_alloc_queues_free(struct Player *player) {
	int capture_streams_no = player->caputre_streams_no;
	int stream_no;
	for (stream_no = 0; stream_no < capture_streams_no; ++stream_no) {
		if (player->packets[stream_no] != NULL) {
			queue_free(player->packets[stream_no]);
			player->packets[stream_no] = NULL;
		}
	}
}

void player_prepare_rgb_frames_free(struct Player *player) {
	if (player->rgb_video_frames != NULL) {
		LOGI(7, "player_set_data_source free_video_frames_queue");
		queue_free(player->rgb_video_frames);
		player->rgb_video_frames = NULL;
	}
}

int player_prepare_rgb_frames(struct DecoderState *decoder_state) {
	struct Player *player = decoder_state->player;

	player->rgb_video_frames = queue_init_with_custom_lock(8,
			(queue_fill_func) player_fill_video_rgb_frame,
			(queue_free_func) player_free_video_rgb_frame,
			decoder_state,
			&player->mutex_queue, &player->cond_queue);
	if (player->rgb_video_frames == NULL) {
		return -ERROR_COULD_NOT_PREPARE_RGB_QUEUE;
	}
	return 0;
}

int player_preapre_sws_context(struct Player *player) {
	AVCodecContext * ctx = player->input_codec_ctxs[player->video_stream_no];

	int destWidth = ctx->width;
	int destHeight = ctx->height;

	player->sws_context = sws_getContext(ctx->width,
			ctx->height,
			ctx->pix_fmt, destWidth, destHeight,
			player->out_format, SWS_BICUBIC, NULL, NULL, NULL);
	if (player->sws_context == NULL) {
		LOGE(1, "could not initialize conversion context\n");
		return -ERROR_COULD_NOT_GET_SWS_CONTEXT;
	}
	return 0;
}

void player_preapre_sws_context_free(struct Player *player) {
	if (player->sws_context != NULL) {
		LOGI(7, "player_set_data_source free_sws_context");
		sws_freeContext(player->sws_context);
		player->sws_context = NULL;
	}
}

void player_create_audio_track_free(struct Player *player, struct State *state) {
	if (player->swr_context != NULL) {
		swr_free(&player->swr_context);
		player->swr_context=NULL;
	}

	if (player->audio_track != NULL) {
		LOGI(7, "player_set_data_source free_audio_track_ref");
		(*state->env)->DeleteGlobalRef(state->env, player->audio_track);
		player->audio_track = NULL;
	}
	if (player->audio_stream_no >= 0) {
		AVCodecContext ** ctx = &player->input_codec_ctxs[player->audio_stream_no];
		if (*ctx != NULL) {
			LOGI(7, "player_set_data_sourceclose_audio_codec");
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
			state->thiz, player->player_prepare_audio_track_method, sample_rate,
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
			|| ctx->sample_rate
					!= audio_track_sample_rate) {

		LOGI(3,
				"player_set_data_sourcd preparing conversion of %d Hz %s %d channels to %d Hz %s %d channels",
				ctx->sample_rate,
				av_get_sample_fmt_name(ctx->sample_fmt),
				ctx->channels,
				audio_track_sample_rate,
				av_get_sample_fmt_name(player->audio_track_format),
				player->audio_track_channel_count);
		player->swr_context = (struct SwrContext *)swr_alloc_set_opts(NULL, audio_track_layout,
				player->audio_track_format, audio_track_sample_rate,
				dec_channel_layout, ctx->sample_fmt,
				ctx->sample_rate, 0, NULL);

		if (!player->swr_context || swr_init(player->swr_context) < 0) {
			LOGE(1,
					"Cannot create sample rate converter for conversion of %d Hz %s %d channels to %d Hz %s %d channels!",
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
			player->video_duration = round(
					stream->duration * av_q2d(stream->time_base));
			LOGI(3,
					"player_set_data_source stream[%d] duration: %ld", i, stream->duration);
			return;
		}
	}
	if (player->input_format_ctx->duration != 0) {
		player->video_duration = round(
				player->input_format_ctx->duration * av_q2d(AV_TIME_BASE_Q));
		LOGI(3,
				"player_set_data_source video duration: %ld", player->input_format_ctx->duration)
		return;
	}

	for (i = 0; i < player->input_format_ctx->nb_streams; i++) {
		AVStream *stream = player->input_format_ctx->streams[i];
		if (stream->duration > 0) {
			player->video_duration = round(
					stream->duration * av_q2d(stream->time_base));
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
		err =  -ERROR_COULD_NOT_INIT_PTHREAD_ATTR;
		goto end;
	}
	for (i = 0; i < player->caputre_streams_no; ++i) {
		struct DecoderData * decoder_data = malloc(sizeof(decoder_data));
		*decoder_data = (struct DecoderData){player: player, stream_no: i};
		ret = pthread_create(&player->decode_threads[i], &attr,
				player_decode, decoder_data);
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

end:
	ret = pthread_attr_destroy(&attr);
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

int player_open_input(struct Player *player, const char *file_path, AVDictionary *dictionary) {
	int ret;
	if ((ret = avformat_open_input(&(player->input_format_ctx), file_path, NULL,
			&dictionary)) < 0) {
		char errbuf[128];
		const char *errbuf_ptr = errbuf;

		if (av_strerror(ret, errbuf, sizeof(errbuf)) < 0)
			errbuf_ptr = strerror(AVUNERROR(ret));

		LOGE(1,
				"player_set_data_source Could not open video file: %s (%d: %s)\n", file_path, ret, errbuf_ptr);
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
	player_preapre_sws_context_free(player);
	player_prepare_rgb_frames_free(player);
	player_alloc_queues_free(player);
	player_alloc_frames_free(player);
	player_find_streams_free(player);
	player_find_stream_info_free(player);
	player_open_input_free(player);
	player_create_context_free(player);
}

void player_stop(struct State * state) {
	int ret;

	pthread_mutex_lock(&state->player->mutex_operation);
	player_stop_without_lock(state);
	pthread_mutex_unlock(&state->player->mutex_operation);
}

int player_set_data_source(struct State *state, const char *file_path, AVDictionary *dictionary) {
	struct Player *player = state->player;
	int err = ERROR_NO_ERROR;
	int i;

	pthread_mutex_lock(&player->mutex_operation);

	player_stop_without_lock(state);

	if (player->playing)
		goto end;

	// initial setup
	player->out_format = PIX_FMT_RGB565;
	player->pause = TRUE;
	player->audio_pause_time = player->audio_resume_time = av_gettime();

	// trying decode video
	if ((err = player_create_context(player)) < 0)
		goto error;

	if ((err = player_open_input(player, file_path, dictionary)) < 0)
		goto error;

	if ((err = player_find_stream_info(player)) < 0)
		goto error;

	player_print_video_informations(player, file_path);

	if ((player->video_stream_no = player_find_stream(player, AVMEDIA_TYPE_VIDEO)) < 0) {
		err = player->video_stream_no;
		goto error;
	}

	if ((player->audio_stream_no = player_find_stream(player, AVMEDIA_TYPE_AUDIO)) < 0) {
		err = player->audio_stream_no;
		goto error;
	}

	if ((err = player_alloc_frames(player)) < 0)
		goto error;

	if ((err = player_alloc_queues(player)) < 0)
		goto error;

	struct DecoderState video_decoder_state = {stream_no: player->video_stream_no, player: player, env:state->env, thiz: state->thiz};
	if ((err = player_prepare_rgb_frames(&video_decoder_state)) < 0)
		goto error;

	if ((err = player_preapre_sws_context(player)) < 0)
		goto error;

	if ((err = player_create_audio_track(player, state)) < 0)
		goto error;

	player_get_video_duration(player);
	player_update_time(state, 0.0);

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
	player_preapre_sws_context_free(player);
	player_prepare_rgb_frames_free(player);
	player_alloc_queues_free(player);
	player_alloc_frames_free(player);
	player_find_streams_free(player);
	player_find_stream_info_free(player);
	player_open_input_free(player);
	player_create_context_free(player);

	end:
	LOGI(7, "player_set_data_source end");
	pthread_mutex_unlock(&player->mutex_operation);
	return err;
}

int player_get_next_frame(int current_frame, int max_frame) {
	return (current_frame + 1) % max_frame;
}

void jni_player_seek(JNIEnv *env, jobject thiz, jint position) {
	struct Player *player = player_get_player_field(env, thiz);
	pthread_mutex_lock(&player->mutex_operation);

	if (!player->playing) {
		LOGI(1, "jni_player_seek could not seek while not playing");
		throw_exception(env, not_playing_exception_class_path_name,
				"Could not pause while not playing");
		goto end;
	}
	pthread_mutex_lock(&player->mutex_queue);
	player->seek_position = position;
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
	(*env)->CallVoidMethod(env, player->audio_track,
			player->audio_track_pause_method);
	player->audio_pause_time = av_gettime();

	// just leave exception

	pthread_cond_broadcast(&player->cond_queue);

	do_nothing: pthread_mutex_unlock(&player->mutex_queue);

	end: pthread_mutex_unlock(&player->mutex_operation);

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
	(*env)->CallVoidMethod(env, player->audio_track,
			player->audio_track_play_method);
	// just leave exception

	player->audio_resume_time = av_gettime();
	if (player->audio_write_time < player->audio_pause_time) {
		player->audio_write_time = player->audio_resume_time;
	} else if (player->audio_write_time < player->audio_resume_time) {
		player->audio_write_time += player->audio_resume_time
				- player->audio_pause_time;
	}

	pthread_cond_broadcast(&player->cond_queue);

	do_nothing: pthread_mutex_unlock(&player->mutex_queue);

	end: pthread_mutex_unlock(&player->mutex_operation);
}

void jni_player_read_dictionary(JNIEnv *env, AVDictionary **dictionary, jobject jdictionary) {
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
		jobject jvalue = (*env)->CallObjectMethod(env, jdictionary, map_get_method,
				jkey);

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

int jni_player_set_data_source(JNIEnv *env, jobject thiz, jstring string, jobject dictionary) {

	AVDictionary *dict = NULL;
	if (dictionary != NULL) {
		jni_player_read_dictionary(env, &dict, dictionary);
		(*env)->DeleteLocalRef(env, dictionary);
	}


	const char *file_path = (*env)->GetStringUTFChars(env, string, NULL);
	struct Player * player = player_get_player_field(env, thiz);
	struct State state = { player: player, env: env, thiz: thiz };

	int ret = player_set_data_source(&state, file_path, dict);

	(*env)->ReleaseStringUTFChars(env, string, file_path);
	return ret;
}

void jni_player_dealloc(JNIEnv *env, jobject thiz) {
	struct Player *player = player_get_player_field(env, thiz);

	(*env)->DeleteGlobalRef(env, player->audio_track_class);
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
	player->rendering = FALSE;

	int err = ERROR_NO_ERROR;

	int ret = (*env)->GetJavaVM(env, &player->get_javavm);
	if (ret) {
		err = ERROR_COULD_NOT_GET_JAVA_VM;
		goto free_player;
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
	pthread_cond_init(&player->cond_queue, NULL);

	player->playing = FALSE;
	player->pause = FALSE;
	player->stop = TRUE;
	player->flush_video_play = FALSE;

	av_register_all();
	register_jni_protocol(player->get_javavm);
#ifdef MODULE_ENCRYPT
	register_aes_protocol();
#endif

	player_print_all_codecs();

	goto end;

	delete_audio_track_global_ref: (*env)->DeleteGlobalRef(env,
			player->audio_track_class);

	free_player: free(player);

	end: return err;
}

enum RenderCheckMsg {
	RENDER_CHECK_MSG_INTERRUPT = 0, RENDER_CHECK_MSG_FLUSH,
};

QueueCheckFuncRet player_render_frame_check_func(Queue *queue,
		struct Player *player, int *check_ret_data) {
	if (player->interrupt_renderer) {
		*check_ret_data = RENDER_CHECK_MSG_INTERRUPT;
		LOGI(6, "player_render_frame_check_func: interrupt_renderer")
		return QUEUE_CHECK_FUNC_RET_SKIP;
	}
	if (player->flush_video_play) {
		LOGI(6, "player_render_frame_check_func: flush_video_play");
		*check_ret_data = RENDER_CHECK_MSG_FLUSH;
		return QUEUE_CHECK_FUNC_RET_SKIP;
	}
	if (player->pause) {
		LOGI(6, "player_render_frame_check_func: pause")

		return QUEUE_CHECK_FUNC_RET_WAIT;
	}
	if (player->stop) {
		LOGI(6, "player_render_frame_check_func: stop")

		return QUEUE_CHECK_FUNC_RET_WAIT;
	}

	LOGI(9, "player_render_frame_check_func: test")
	return QUEUE_CHECK_FUNC_RET_TEST;
}

void jni_player_render_frame_start(JNIEnv *env, jobject thiz) {
	struct Player * player = player_get_player_field(env, thiz);
	pthread_mutex_lock(&player->mutex_queue);
	assert(!player->rendering);
	player->rendering = TRUE;
	player->interrupt_renderer = FALSE;
	pthread_cond_broadcast(&player->cond_queue);
	pthread_mutex_unlock(&player->mutex_queue);
}

void jni_player_render_frame_stop(JNIEnv *env, jobject thiz) {
	struct Player * player = player_get_player_field(env, thiz);
	pthread_mutex_lock(&player->mutex_queue);
	assert(player->rendering);
	player->rendering = FALSE;
	player->interrupt_renderer = TRUE;
	pthread_cond_broadcast(&player->cond_queue);
	pthread_mutex_unlock(&player->mutex_queue);
}

jobject jni_player_render_frame(JNIEnv *env, jobject thiz) {
	struct Player * player = player_get_player_field(env, thiz);
	struct State state = { env: env, player: player, thiz: thiz };
	int interrupt_ret;
	struct VideoRGBFrameElem *elem;

	LOGI(7, "jni_player_render_frame render wait...");
	pthread_mutex_lock(&player->mutex_queue);
	pop:
	LOGI(4, "jni_player_render_frame reading from queue");
	elem = queue_pop_start_already_locked(player->rgb_video_frames,
			(QueueCheckFunc) player_render_frame_check_func, player,
			&interrupt_ret);
	for (;;) {
		int skip = FALSE;
		if (elem == NULL) {
			skip = TRUE;
		} else {
			QueueCheckFuncRet ret;
			test: ret = player_render_frame_check_func(player->rgb_video_frames,
					player, &interrupt_ret);
			switch (ret) {
			case QUEUE_CHECK_FUNC_RET_WAIT:
				pthread_cond_wait(&player->cond_queue, &player->mutex_queue);
				goto test;
			case QUEUE_CHECK_FUNC_RET_SKIP:
				skip = TRUE;
				queue_pop_finish_already_locked(player->rgb_video_frames);
				break;
			case QUEUE_CHECK_FUNC_RET_TEST:
				break;
			default:
				assert(FALSE);
				break;
			}
		}
		if (skip) {
			if (interrupt_ret == RENDER_CHECK_MSG_INTERRUPT) {
				LOGI(2, "jni_player_render_frame interrupted");
				pthread_mutex_unlock(&player->mutex_queue);

				throw_interrupted_exception(env,
						"Render frame was interrupted by user");
				return NULL;
			} else if (interrupt_ret == RENDER_CHECK_MSG_FLUSH) {
				LOGI(2, "jni_player_render_frame flush");
				struct VideoRGBFrameElem *elem;
				while ((elem = queue_pop_start_already_locked_non_block(
						player->rgb_video_frames)) != NULL) {
					queue_pop_finish_already_locked(player->rgb_video_frames);
				}
				LOGI(2, "jni_player_render_frame flushed");
				player->flush_video_play = FALSE;
				pthread_cond_broadcast(&player->cond_queue);
				goto pop;
			} else {
				assert(FALSE);
			}
		}

		int64_t current_time = av_gettime();
		int64_t time_diff = current_time - player->audio_write_time;
		double pts_time_diff_d = elem->time - player->audio_clock;
		int64_t sleep_time = (int64_t) (pts_time_diff_d * 1000.0)
				- (int64_t) (time_diff / 1000L);

		LOGI(9,
				"jni_player_render_frame current_time: %lld, write_time: %lld, time_diff: %lld, elem->time: %f, player->audio_clock: %f", current_time, player->audio_write_time, time_diff, elem->time, player->audio_clock);

		LOGI(9, "jni_player_render_frame sleep_time: %ld", sleep_time);
		if (sleep_time <= MIN_SLEEP_TIME_MS) {
			break;
		}

		if (sleep_time > 1000) {
			sleep_time = 1000;
		}

		int ret = pthread_cond_timeout_np(&player->cond_queue,
				&player->mutex_queue, sleep_time);
		if (ret == ETIMEDOUT) {
			LOGI(9, "jni_player_render_frame timeout");
			break;
		}
		LOGI(9, "jni_player_render_frame cond occure");
	}
	player_update_time(&state, elem->time);
	pthread_mutex_unlock(&player->mutex_queue);

	LOGI(7, "jni_player_render_frame rendering...");
	return elem->jbitmap;
}

void jni_player_release_frame(JNIEnv *env, jobject thiz) {
	struct Player * player = player_get_player_field(env, thiz);
	queue_pop_finish(player->rgb_video_frames);
	LOGI(7, "jni_player_release_frame rendered");
}

void jni_player_stop(JNIEnv *env, jobject thiz) {
#ifdef PROFILER
	moncleanup();
#endif

	struct Player * player = player_get_player_field(env, thiz);
	struct State state;

	state.player = player;
	state.env = env;
	state.thiz = thiz;

	player_stop(&state);
}

int jni_player_get_video_duration(JNIEnv *env, jobject thiz) {
	struct Player * player = player_get_player_field(env, thiz);
	return player->video_duration;
}


