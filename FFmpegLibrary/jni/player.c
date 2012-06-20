/*
 * player.c
 *
 *  Created on: Mar 19, 2012
 *      Author: Jacek Marchwicki <jacek.marchwicki@gmail.com>
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

#include <android/log.h>
#include <android/bitmap.h>

#include <jni.h>
#include <pthread.h>

/*android profiler*/
#ifdef PROFILER
#include <android-ndk-profiler-3.1/prof.h>
#endif

#ifdef YUV2RGB
#include <yuv2rgb/yuv2rgb.h>
#endif

/*local headers*/

#define MIN_SLEEP_TIME_US 10000 // 1000000 us = 1 s

#include "player.h"

#include "queue.h"

#define LOG_LEVEL 1
#define LOG_TAG "player.c"
#define LOGI(level, ...) if (level <= LOG_LEVEL) {__android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__);}
#define LOGE(level, ...) if (level <= LOG_LEVEL) {__android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__);}

#define FALSE 0
#define TRUE (!(FALSE))

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
	LOGI(10, "id: %d codec: %s, type: %s", codec->id, codec->name, type);
}

struct VideoRGBFrameElem {

	AVFrame *frame;
	jobject jbitmap;
	double time;
};

struct Player {
	AVFormatContext *input_format_ctx;
	AVStream *input_video_stream;
	AVCodecContext *input_video_codec_ctx;

	int input_video_stream_number;
	AVCodec *input_video_codec;

	AVFrame *input_audio_frame;
	AVFrame *input_video_frame;

	enum PixelFormat out_format;

	int input_audio_stream_number;
	AVCodec *input_audio_codec;
	AVStream *input_audio_stream;
	AVCodecContext *input_audio_codec_ctx;

	Queue *video_packets;
	Queue *audio_packets;
	Queue *rgb_video_frames;

	jobject audio_track;

	jclass android_track_class;
	jmethodID auiod_track_prepare_write;
	jmethodID prepare_frame_method;

	int hasFrame;

	pthread_cond_t sync_wait_for_frame_cond;
	pthread_mutex_t sync_mutex;

	struct SwsContext *sws_context;


	pthread_mutex_t audio_clock_mutex;
	double audio_clock;
	int64_t audio_write_time;

	double video_duration;

#ifdef YUV2RGB
	int dither;
#endif
};

struct State {
	struct Player *player;
	JNIEnv* env;
	jobject thiz;
};

enum PlayerErrors {
	ERROR_NO_ERROR = 0,
	ERROR_COULD_NOT_CREATE_AVCONTEXT,
	ERROR_COULD_NOT_OPEN_VIDEO_FILE,
	ERROR_COULD_NOT_OPEN_STREAM,
	ERROR_COULD_NOT_OPEN_VIDEO_STREAM,
	ERROR_COULD_NOT_FIND_VIDEO_CODEC,
	ERROR_COULD_NOT_OPEN_VIDEO_CODEC,
	ERROR_COULD_NOT_ALLOC_FRAME,

	ERROR_NOT_FOUND_PREPARE_FRAME_METHOD,
	ERROR_NOT_CREATED_BITMAP,
	ERROR_COULD_NOT_GET_SWS_CONTEXT,

	ERROR_COULD_NOT_FIND_AUDIO_STREAM,
	ERROR_COULD_NOT_FIND_AUDIO_CODEC,
	ERROR_COULD_NOT_OPEN_AUDIO_CODEC,
	ERROR_COULD_NOT_PREPARE_RGB_QUEUE,
	ERROR_COULD_NOT_PREPARE_AUDIO_PACKETS_QUEUE,
	ERROR_COULD_NOT_PREPARE_VIDEO_PACKETS_QUEUE,

	ERROR_WHILE_DUPLICATING_FRAME,

	ERROR_WHILE_DECODING_VIDEO,
	ERROR_WHILE_ALLOCATING_AUDIO_SAMPLE,
	ERROR_WHILE_DECODING_AUDIO_FRAME,
	ERROR_NOT_FOUND_PREPARE_AUDIO_TRACK_METHOD,
	ERROR_NOT_CREATED_AUDIO_TRACK,
	ERROR_NOT_CREATED_AUDIO_SAMPLE_BYTE_ARRAY,
	ERROR_PLAYING_AUDIO,
	ERROR_WHILE_LOCING_BITMAP,
};

void player_print_all_codecs() {
	AVCodec *p = NULL;
	LOGI(10, "available codecs:");
	while ((p = av_codec_next(p)) != NULL) {
		player_print_codec_description(p);
	}
}

jfieldID player_get_player_field(JNIEnv *env) {
	jclass clazz = (*env)->FindClass(env, player_class_path_name);
	return (*env)->GetFieldID(env, clazz, "mNativePlayer", "I");
}

struct Player * player_get_player(JNIEnv *env, jobject thiz) {

	jfieldID m_native_layer_field = player_get_player_field(env);
	return (struct Player *) (*env)->GetIntField(env, thiz,
			m_native_layer_field);
}

void *player_fill_packet(struct Player *player) {
	AVPacket *packet = malloc(sizeof(AVPacket));
	return packet;
}

void player_free_packet(struct Player *player, AVPacket *elem) {
	free(elem);
}

void player_free_video_rgb_frame(struct State *state,
		struct VideoRGBFrameElem *elem) {
	JNIEnv *env = state->env;
	jobject thiz = state->thiz;

	(*env)->DeleteGlobalRef(env, elem->jbitmap);
	av_free(elem->frame);
	free(elem);
}

void *player_fill_video_rgb_frame(struct State * state) {
	struct Player *player = state->player;
	JNIEnv *env = state->env;
	jobject thiz = state->thiz;

	struct VideoRGBFrameElem * elem = malloc(sizeof(struct VideoRGBFrameElem));

	if (elem == NULL) {
		goto error;
	}

	elem->frame = avcodec_alloc_frame();
	if (elem->frame == NULL) {
		LOGE(1, "could not create frame")
		goto free_elem;
	}

	int destWidth = player->input_video_codec_ctx->width;
	int destHeight = player->input_video_codec_ctx->height;

	elem->jbitmap = (*env)->CallObjectMethod(env, thiz,
			player->prepare_frame_method, destWidth, destHeight);

	if (elem->jbitmap == NULL) {
		LOGE(1, "could not create jbitmap");
		goto free_frame;
	}

	elem->jbitmap = (*env)->NewGlobalRef(env, elem->jbitmap);

	goto end;

	release_ref: (*env)->DeleteGlobalRef(env, elem->jbitmap);
	elem->jbitmap = NULL;

	free_frame: av_free(elem->frame);
	elem->frame = NULL;

	free_elem: free(elem);
	elem = NULL;

	error: end: return elem;

}

int jni_player_set_data_source(JNIEnv *env, jobject thiz, jstring string) {
#ifdef PROFILER
#warning "Profiler enabled"
	setenv("CPUPROFILE_FREQUENCY", "1000", 1);
	monstartup("libffmpeg.so");
#endif

	const char *_filePath = (*env)->GetStringUTFChars(env, string, NULL);

	struct Player *player = malloc(sizeof(struct Player));

	struct State state;
	state.player = player;
	state.env = env;
	state.thiz = thiz;

	jfieldID m_native_layer_field = player_get_player_field(env);
	(*env)->SetIntField(env, thiz, m_native_layer_field, (jint) player);

	int err = ERROR_NO_ERROR;

	jclass clazz = (*env)->FindClass(env, player_class_path_name);
	player->prepare_frame_method = (*env)->GetMethodID(env, clazz, "prepareFrame",
			"(II)Landroid/graphics/Bitmap;");
	if (player->prepare_frame_method == NULL) {
		err = ERROR_NOT_FOUND_PREPARE_FRAME_METHOD;
		goto end;
	}

	jmethodID prepare_audio_track_method = (*env)->GetMethodID(env, clazz,
			"prepareAudioTrack", "(II)Landroid/media/AudioTrack;");
	if (prepare_audio_track_method == NULL) {
		err = ERROR_NOT_FOUND_PREPARE_AUDIO_TRACK_METHOD;
		goto end;
	}

	player->android_track_class = (*env)->FindClass(env,
			android_track_class_path_name);
	player->auiod_track_prepare_write = (*env)->GetMethodID(env,
			player->android_track_class, "write", "([BII)I");

	pthread_cond_init(&player->sync_wait_for_frame_cond, NULL);
	pthread_mutex_init(&player->sync_mutex, NULL);
	pthread_mutex_init(&player->audio_clock_mutex, NULL);

	player->hasFrame = FALSE;

	av_register_all();
	player_print_all_codecs();

	player->input_format_ctx = avformat_alloc_context();
	if (player->input_format_ctx == NULL) {
		LOGE(1, "Could not create AVContext\n");
		err = ERROR_COULD_NOT_CREATE_AVCONTEXT;
		goto end;
	}

	// open stream/file
	int ret;
	if ((ret = avformat_open_input(&(player->input_format_ctx), _filePath, NULL, NULL))
			< 0) {
		char errbuf[128];
		const char *errbuf_ptr = errbuf;

		if (av_strerror(ret, errbuf, sizeof(errbuf)) < 0)
			errbuf_ptr = strerror(AVUNERROR(err));

		LOGE(1,
				"Could not open video file: %s (%d: %s)\n", _filePath, ret, errbuf_ptr);
		err = ERROR_COULD_NOT_OPEN_VIDEO_FILE;
		goto remove_context;
	}

	// find video informations
	if (avformat_find_stream_info(player->input_format_ctx, NULL) < 0) {
		LOGE(1, "Could not open stream\n");
		err = ERROR_COULD_NOT_OPEN_STREAM;
		goto close_file;
	}

	// print video informations
	av_dump_format(player->input_format_ctx, 0, _filePath, FALSE);

	//find video stream
	int videoStream = -1;
	int i;
	for (i = 0; i < player->input_format_ctx->nb_streams; i++) {
		AVStream *stream = player->input_format_ctx->streams[i];
		AVCodecContext *codec = stream->codec;
		if (codec->codec_type == AVMEDIA_TYPE_VIDEO) {
			videoStream = i;
			break;
		}
	}
	if (videoStream < 0) {
		err = ERROR_COULD_NOT_OPEN_VIDEO_STREAM;
		goto close_file;
	}

	player->input_video_stream = player->input_format_ctx->streams[videoStream];
	player->input_video_codec_ctx = player->input_video_stream->codec;
	player->input_video_stream_number = videoStream;

	LOGI(5,
			"Video size is [%d x %d]", player->input_video_codec_ctx->width, player->input_video_codec_ctx->height);

	AVCodec *codec = avcodec_find_decoder(player->input_video_codec_ctx->codec_id);
	if (codec == NULL) {
		LOGE(1,
				"Could not find video codec for id: %d", player->input_video_codec_ctx->codec_id);
		err = ERROR_COULD_NOT_FIND_VIDEO_CODEC;
		player_print_all_codecs();
		goto close_file;
	}

	if (avcodec_open2(player->input_video_codec_ctx, codec, NULL) < 0) {
		LOGE(1, "Could not open video codec");
		player_print_codec_description(codec);
		err = ERROR_COULD_NOT_OPEN_VIDEO_CODEC;
		goto close_file;
	}
	player->input_video_codec = codec;

	player->input_video_frame = avcodec_alloc_frame();
	if (player->input_video_frame == NULL) {
		err = ERROR_COULD_NOT_ALLOC_FRAME;
		goto close_video_codec;
	}

	player->input_audio_frame = avcodec_alloc_frame();
	if (player->input_audio_frame == NULL) {
		err = ERROR_COULD_NOT_ALLOC_FRAME;
		goto free_video_frame;
	}

	player->audio_packets = queue_init(100, (queue_fill_func) player_fill_packet,
			(queue_free_func) player_free_packet, player);
	if (player->audio_packets == NULL) {
		err = ERROR_COULD_NOT_PREPARE_AUDIO_PACKETS_QUEUE;
		goto free_audio_frame;
	}

	player->video_packets = queue_init(100, (queue_fill_func) player_fill_packet,
			(queue_free_func) player_free_packet, player);
	if (player->video_packets == NULL) {
		err = ERROR_COULD_NOT_PREPARE_VIDEO_PACKETS_QUEUE;
		goto free_audio_packets;
	}

	player->out_format = PIX_FMT_RGB565;

	player->rgb_video_frames = queue_init(8,
			(queue_fill_func) player_fill_video_rgb_frame,
			(queue_free_func) player_free_video_rgb_frame, &state);
	if (player->rgb_video_frames == NULL) {
		err = ERROR_COULD_NOT_PREPARE_RGB_QUEUE;
		goto free_video_packets;
	}

	int destWidth = player->input_video_codec_ctx->width;
	int destHeight = player->input_video_codec_ctx->height;

	player->sws_context = sws_getContext(player->input_video_codec_ctx->width,
			player->input_video_codec_ctx->height, player->input_video_codec_ctx->pix_fmt,
			destWidth, destHeight, player->out_format, SWS_BICUBIC, NULL, NULL,
			NULL);
	if (player->sws_context == NULL) {
		LOGE(1, "could not initialize conversion context\n");
		err = ERROR_COULD_NOT_GET_SWS_CONTEXT;
		goto free_video_frames_queue;
	}

	//find audio stream
	int audioStream = -1;
	for (i = 0; i < player->input_format_ctx->nb_streams; i++) {
		AVStream *stream = player->input_format_ctx->streams[i];
		AVCodecContext *codec = stream->codec;
		if (codec->codec_type == AVMEDIA_TYPE_AUDIO) {
			audioStream = i;
			break;
		}
	}
	if (audioStream < 0) {
		err = ERROR_COULD_NOT_FIND_AUDIO_STREAM;
		goto free_sws_context;
	}

	player->input_audio_stream = player->input_format_ctx->streams[audioStream];
	player->input_audio_codec_ctx = player->input_audio_stream->codec;
	player->input_audio_stream_number = audioStream;

	AVCodec *audioCodec = avcodec_find_decoder(
			player->input_audio_codec_ctx->codec_id);
	if (audioCodec == NULL) {
		LOGE(1,
				"Could not find audio codec for id: %d", player->input_audio_codec_ctx->codec_id);
		err = ERROR_COULD_NOT_FIND_AUDIO_CODEC;
		player_print_all_codecs();
		goto free_sws_context;
	}

	if (avcodec_open2(player->input_audio_codec_ctx, audioCodec, NULL) < 0) {
		LOGE(1, "Could not open audio codec");
		player_print_codec_description(audioCodec);
		err = ERROR_COULD_NOT_OPEN_AUDIO_CODEC;
		goto free_sws_context;
	}
	player->input_audio_codec = audioCodec;

	//creating audiotrack
	AVCodecContext *audioCodecCtx = player->input_audio_codec_ctx;
	int sample_rate = audioCodecCtx->sample_rate;
	int channels = audioCodecCtx->channels;

	player->audio_track = (*env)->CallObjectMethod(env, thiz,
			prepare_audio_track_method, sample_rate, channels);

	if (player->audio_track == NULL) {
		err = ERROR_NOT_CREATED_AUDIO_TRACK;
		goto close_audio_codec;
	}
	player->audio_track = (*env)->NewGlobalRef(env, player->audio_track);


	player->video_duration = (double) player->input_video_stream->duration
			* av_q2d(player->input_video_stream->time_base);

// SUCCESS

	goto end;

	free_audio_track_ref: (*env)->DeleteGlobalRef(env, player->audio_track);
	player->audio_track = NULL;

	close_audio_codec: avcodec_close(player->input_audio_codec_ctx);
	player->input_audio_codec_ctx = NULL;

	free_sws_context: sws_freeContext(player->sws_context);
	player->sws_context = NULL;

	free_video_frames_queue: queue_free(player->rgb_video_frames);

	free_video_packets: queue_free(player->video_packets);

	free_audio_packets: queue_free(player->audio_packets);

	free_audio_frame: av_free(player->input_audio_frame);
	player->input_audio_frame = NULL;

	free_video_frame: av_free(player->input_video_frame);
	player->input_video_frame = NULL;

	close_video_codec: avcodec_close(player->input_video_codec_ctx);
	player->input_video_codec = NULL;

	close_file:

	remove_context: av_free(player->input_format_ctx);
	player->input_format_ctx = NULL;

	end: return err;
}

//void player_fill_bitmap(JNIEnv *env, struct Player * vc, AVFrame *frame) {
//
//	AndroidBitmapInfo info;
//	void* pixels;
//	int ret;
//
//	if ((ret = AndroidBitmap_getInfo(env, vc->bitmap, &info)) < 0) {
//		LOGE(1, "AndroidBitmap_getInfo() failed ! error=%d", ret);
//		return;
//	}
//
//	if ((ret = AndroidBitmap_lockPixels(env, vc->bitmap, &pixels)) < 0) {
//		LOGE(1, "AndroidBitmap_lockPixels() failed ! error=%d", ret);
//	}
//
//
//	LOGI(7, "copying...");
//
//	uint8_t * frameLine;
//	uint32_t yy;
//	for (yy = 0; yy < info.height; yy++) {
//		uint8_t* line = (uint8_t*) pixels;
//		frameLine = (uint8_t *) frame->data[0] + (yy * frame->linesize[0]);
//		memcpy(line, frameLine, frame->linesize[0]);
//		pixels = (char*) pixels + info.stride;
//	}
//
//	//memcpy(pixels, frame->data[0], frame->linesize[0] * info.height);
//
//end:
//
//	vc->hasFrame = TRUE;
//
//	AndroidBitmap_unlockPixels(env, vc->bitmap);
//
//}

int player_get_next_frame(int current_frame, int max_frame) {
	return (current_frame + 1) % max_frame;
}

int player_write_audio(struct Player *player, JNIEnv *env) {
	AVFrame * frame = player->input_audio_frame;
	int err;
	int ret;
	LOGI(10, "Writing audio frame")
	AVCodecContext * c = player->input_audio_codec_ctx;
	int data_size = av_samples_get_buffer_size(NULL, c->channels,
			frame->nb_samples, c->sample_fmt, 1);

	jbyteArray samples_byte_array = (*env)->NewByteArray(env, data_size);
	if (samples_byte_array == NULL) {
		err = ERROR_NOT_CREATED_AUDIO_SAMPLE_BYTE_ARRAY;
		goto end;
	}

	pthread_mutex_lock(&player->audio_clock_mutex);

	player->audio_clock += (double) data_size
			/ (c->channels * c->sample_rate
			* av_get_bytes_per_sample(c->sample_fmt));


	player->audio_write_time = av_gettime();

	pthread_mutex_unlock(&player->audio_clock_mutex);

	LOGI(10, "Writing sample data")

	jbyte *jni_samples = (*env)->GetByteArrayElements(env, samples_byte_array,
			NULL);
	memcpy(jni_samples, frame->data[0], data_size);
	(*env)->ReleaseByteArrayElements(env, samples_byte_array, jni_samples, 0);

	LOGI(10, "playing audio track");
	ret = (*env)->CallIntMethod(env, player->audio_track,
			player->auiod_track_prepare_write, samples_byte_array, 0,
			data_size);
	if (ret < 0) {
		err = ERROR_PLAYING_AUDIO;
		goto end;
	}

	LOGI(10, "releasing local ref");
	(*env)->DeleteLocalRef(env, samples_byte_array);

	end: return err;
}

int player_decode_audio(struct Player *player, JNIEnv *env) {
	int err = 0;
	player->audio_clock = 0.0;
	for (;;) {
		LOGI(10, "Waiting for audio frame\n");
		AVPacket *packet = queue_pop_start(player->audio_packets);
		LOGI(10, "Decoding audio frame\n");

		int got_frame_ptr;
		int len = avcodec_decode_audio4(player->input_audio_codec_ctx,
				player->input_audio_frame, &got_frame_ptr, packet);

		av_free_packet(packet);
		queue_pop_finish(player->audio_packets);

		if (len < 0) {
			LOGE(1, "Fail decoding audio %d\n", len);
			err = ERROR_WHILE_DECODING_VIDEO;
			goto end;
		}
		if (!got_frame_ptr) {
			LOGI(10, "Audio frame not finished\n");
			continue;
		}

		LOGI(10, "Decoded audio frame\n");

		if ((err = player_write_audio(player, env)) < 0) {
			LOGI(10, "Could not write frame");
			goto end;
		}

	}

	end: return err;
}

int player_decode_video(struct State state) {
	struct Player *player = state.player;
	JNIEnv *env = state.env;
	jobject thiz = state.thiz;

	int queueSize = queue_get_size(player->video_packets);
	queue_wait_for(player->video_packets, queueSize / 2);
	int err = 0;
	for (;;) {
		LOGI(10, "Waiting for video frame\n");
		AVPacket *packet = queue_pop_start(player->video_packets);

		LOGI(10, "Decoding video frame\n");
		int frameFinished;
		int ret = avcodec_decode_video2(player->input_video_codec_ctx,
				player->input_video_frame, &frameFinished, packet);

		av_free_packet(packet);
		queue_pop_finish(player->video_packets);

		if (ret < 0) {
			LOGE(1, "Fail decoding video %d\n", ret);
			err = ERROR_WHILE_DECODING_VIDEO;
			goto end_loop;
		}
		if (!frameFinished) {
			LOGI(10, "Video frame not finished\n");
			goto end_loop;
		}

		int64_t pts = av_frame_get_best_effort_timestamp(
				player->input_video_frame);
		if (pts == AV_NOPTS_VALUE) {
			pts = 0;
		}
		double time = (double) pts
				* av_q2d(player->input_video_stream->time_base);
//		int64_t delay = av_q2d(player->inputVideoStream->codec->time_base) * 0.5 * player->inputVideoFrame->repeat_pict;
		LOGI(10, "Decoded video frame: %f\n", time);

		AVCodecContext * pCodecCtx = player->input_video_codec_ctx;

		// saving in buffer converted video frame
		LOGI(7, "copy wait");
		int to_write;
		struct VideoRGBFrameElem * elem = queue_push_start(
				player->rgb_video_frames, &to_write);

		elem->time = time;
		AVFrame * rgbFrame = elem->frame;
		AVFrame * frame = player->input_video_frame;
		void *buffer;
		int destWidth = player->input_video_codec_ctx->width;
		int destHeight = player->input_video_codec_ctx->height;

		if ((ret = AndroidBitmap_lockPixels(env, elem->jbitmap, &buffer)) < 0) {
			LOGE(1, "AndroidBitmap_lockPixels() failed ! error=%d", ret);
			err = ERROR_WHILE_LOCING_BITMAP;
			goto fail_lock_bitmap;
		}

		avpicture_fill((AVPicture *) elem->frame, buffer, player->out_format,
				destWidth, destHeight);

		LOGI(7, "copying...");
#ifdef YUV2RGB
		yuv420_2_rgb565(rgbFrame->data[0], frame->data[0], frame->data[1],
				frame->data[2], destWidth, destHeight, frame->linesize[0],
				frame->linesize[1], destWidth << 1, yuv2rgb565_table,
				player->dither++);
#else
		sws_scale(player->sws_context,
				(const uint8_t * const *) player->input_video_frame->data,
				player->input_video_frame->linesize, 0, pCodecCtx->height,
				rgbFrame->data,
				rgbFrame->linesize);
#endif

		AndroidBitmap_unlockPixels(env, elem->jbitmap);

		fail_lock_bitmap:

		queue_push_finish(player->rgb_video_frames, to_write);
		(7, "copied");

		end_loop: if (err)
			goto end;
	}

	end: return err;
}

int player_play(struct Player *player) {
	int err = ERROR_NO_ERROR;

	AVPacket packet, *pkt = &packet;

	while (av_read_frame(player->input_format_ctx, pkt) >= 0) {

		if (packet.stream_index == player->input_video_stream_number) {
			LOGI(10, "Read video frame\n");

			int to_write;
			AVPacket * new_packet = queue_push_start(player->video_packets,
					&to_write);
			*new_packet = packet;

			if (av_dup_packet(new_packet) < 0) {
				err = ERROR_WHILE_DUPLICATING_FRAME;
				av_free_packet(pkt);
				goto end_loop;
			}

			queue_push_finish(player->video_packets, to_write);
		} else if (packet.stream_index == player->input_audio_stream_number) {
			LOGI(10, "Read audio frame\n");

			int to_write;
			AVPacket * new_packet = queue_push_start(player->audio_packets,
					&to_write);
			*new_packet = packet;

			if (av_dup_packet(new_packet) < 0) {
				err = ERROR_WHILE_DUPLICATING_FRAME;
				av_free_packet(pkt);
				goto end_loop;
			}

			queue_push_finish(player->audio_packets, to_write);
		} else {
			av_free_packet(pkt);
		}

		end_loop: if (err)
			goto end;
	}
	end: return err;
}

jobject jni_player_render_frame(JNIEnv *env, jobject thiz) {
	struct Player * player = player_get_player(env, thiz);

	LOGI(7, "render wait...");
	struct VideoRGBFrameElem *elem = queue_pop_start(player->rgb_video_frames);

	LOGI(7, "sleep wait...")
	pthread_mutex_lock(&player->audio_clock_mutex);
	int64_t time_diff = av_gettime() - player->audio_write_time;

	double pts_time_diff_d =  elem->time - player->audio_clock;

	pthread_mutex_unlock(&player->audio_clock_mutex);


	int64_t pts_time_diff = pts_time_diff_d * 1000000L;
	int64_t sleep_time = pts_time_diff - time_diff;

	LOGI(11, "time_diff: %ld, pts_time_diff_d: %f, pts_time_diff: %ld, sleep_time: %ld", (long)time_diff, pts_time_diff_d, (long)pts_time_diff, (long)sleep_time);

	if (sleep_time > MIN_SLEEP_TIME_US) {
		LOGI(7, "waiting %ld us", (long) sleep_time);
		usleep(sleep_time);
	}

	LOGI(7, "rendering...");
	return elem->jbitmap;
}

void jni_player_release_frame(JNIEnv *env, jobject thiz) {

	struct Player * player = player_get_player(env, thiz);
	queue_pop_finish(player->rgb_video_frames);
	LOGI(7, "rendered");

}

int jni_player_decode_audio(JNIEnv *env, jobject thiz) {
	struct Player * player = player_get_player(env, thiz);
	return player_decode_audio(player, env);
}

int jni_player_play(JNIEnv *env, jobject thiz) {
	struct Player * player = player_get_player(env, thiz);
	return player_play(player);
}

void jni_player_stop(JNIEnv *env, jobject thiz) {
#ifdef PROFILER
	moncleanup();
#endif
}

double jni_player_get_video_duration(JNIEnv *env, jobject thiz) {
	struct Player * player = player_get_player(env, thiz);
	return player->video_duration;
}

int jni_player_decode_video(JNIEnv *env, jobject thiz) {
	struct State state;
	state.player = player_get_player(env, thiz);
	state.env = env;
	state.thiz = thiz;

	return player_decode_video(state);
}

