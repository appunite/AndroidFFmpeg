/*
 * player.h
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

#ifndef H_PLAYER
#define H_PLAYER

#include <libavutil/audioconvert.h>

static JavaMethod empty_constructor = {"<init>", "()V"};

// InterruptedException
static char *interrupted_exception_class_path_name = "java/lang/InterruptedException";

// RuntimeException
static char *runtime_exception_class_path_name = "java/lang/RuntimeException";

// NotPlayingException
static char *not_playing_exception_class_path_name = "com/appunite/ffmpeg/NotPlayingException";

// Object
static char *object_class_path_name = "java/lang/Object";

// HashMap
static char *hash_map_class_path_name = "java/util/HashMap";
static char *map_class_path_name = "java/util/Map";
static JavaMethod map_key_set = {"keySet", "()Ljava/util/Set;"};
static JavaMethod map_get = {"get", "(Ljava/lang/Object;)Ljava/lang/Object;"};
static JavaMethod map_put = {"put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;"};

// FFmpegStreamInfo.CodeType
enum CodecType {
	CODEC_TYPE_UNKNOWN = 0,
	CODEC_TYPE_AUDIO = 1,
	CODEC_TYPE_VIDEO = 2,
	CODEC_TYPE_SUBTITLE = 3,
	CODEC_TYPE_ATTACHMENT = 4,
	CODEC_TYPE_NB = 5,
	CODEC_TYPE_DATA = 6
};

enum StreamNumber {
	NO_STREAM = -2,
	UNKNOWN_STREAM = -1,
};

// FFmpegStreamInfo
static char *stream_info_class_path_name = "com/appunite/ffmpeg/FFmpegStreamInfo";
static JavaMethod steram_info_set_metadata = {"setMetadata", "(Ljava/util/Map;)V"};
static JavaMethod steram_info_set_media_type_internal = {"setMediaTypeInternal", "(I)V"};
static JavaMethod stream_info_set_stream_number = {"setStreamNumber", "(I)V"};


// Set
static char *set_class_path_name = "java/util/Set";
static JavaMethod set_iterator = {"iterator", "()Ljava/util/Iterator;"};

// Iterator
static char *iterator_class_path_name = "java/util/Iterator";
static JavaMethod iterator_next = {"next", "()Ljava/lang/Object;"};
static JavaMethod iterator_has_next = {"hasNext", "()Z"};

static const struct {
    const char *name;
    int         nb_channels;
    uint64_t     layout;
} channel_android_layout_map[] = {
    { "mono",        1,  AV_CH_LAYOUT_MONO },
    { "stereo",      2,  AV_CH_LAYOUT_STEREO },
    { "2.1",         3,  AV_CH_LAYOUT_2POINT1 },
    { "4.0",         4,  AV_CH_LAYOUT_4POINT0 },
    { "4.1",         5,  AV_CH_LAYOUT_4POINT1 },
    { "5.1",         6,  AV_CH_LAYOUT_5POINT1_BACK },
    { "6.0",         6,  AV_CH_LAYOUT_6POINT0 },
    { "7.0(front)",  7,  AV_CH_LAYOUT_7POINT0_FRONT },
    { "7.1",         8,  AV_CH_LAYOUT_7POINT1 },
};


// FFmpegPlayer
static char *player_class_path_name = "com/appunite/ffmpeg/FFmpegPlayer";
static JavaField player_m_native_player = {"mNativePlayer", "I"};
static JavaMethod player_on_update_time = {"onUpdateTime","(JJZ)V"};
static JavaMethod player_prepare_audio_track = {"prepareAudioTrack", "(II)Landroid/media/AudioTrack;"};
static JavaMethod player_prepare_frame = {"prepareFrame", "(II)Landroid/graphics/Bitmap;"};
static JavaMethod player_set_stream_info = {"setStreamsInfo", "([Lcom/appunite/ffmpeg/FFmpegStreamInfo;)V"};

// AudioTrack
static char *android_track_class_path_name = "android/media/AudioTrack";
static JavaMethod audio_track_write = {"write", "([BII)I"};
static JavaMethod audio_track_pause = {"pause", "()V"};
static JavaMethod audio_track_play = {"play", "()V"};
static JavaMethod audio_track_flush = {"flush", "()V"};
static JavaMethod audio_track_stop = {"stop", "()V"};
static JavaMethod audio_track_get_channel_count = {"getChannelCount", "()I"};
static JavaMethod audio_track_get_sample_rate = {"getSampleRate", "()I"};


// Player

int jni_player_init(JNIEnv *env, jobject thiz);
void jni_player_dealloc(JNIEnv *env, jobject thiz);

void jni_player_seek(JNIEnv *env, jobject thiz, jlong positionUs);

void jni_player_pause(JNIEnv *env, jobject thiz);
void jni_player_resume(JNIEnv *env, jobject thiz);

int jni_player_set_data_source(JNIEnv *env, jobject thiz, jstring string,
		jobject dictionary, int video_stream_no, int audio_stream_no,
		int subtitle_stream_no);
void jni_player_stop(JNIEnv *env, jobject thiz);

void jni_player_render_frame_start(JNIEnv *env, jobject thiz);
void jni_player_render_frame_stop(JNIEnv *env, jobject thiz);

jlong jni_player_get_video_duration(JNIEnv *env, jobject thiz);
void jni_player_render(JNIEnv *env, jobject thiz, jobject surface);

static JNINativeMethod player_methods[] = {

	{"initNative", "()I", (void*) jni_player_init},
	{"deallocNative", "()V", (void*) jni_player_dealloc},

	{"seekNative", "(J)V", (void*) jni_player_seek},

	{"pauseNative", "()V", (void*) jni_player_pause},
	{"resumeNative", "()V", (void*) jni_player_resume},

	{"setDataSourceNative", "(Ljava/lang/String;Ljava/util/Map;III)I", (void*) jni_player_set_data_source},
	{"stopNative", "()V", (void*) jni_player_stop},

	{"renderFrameStart", "()V", (void*) jni_player_render_frame_start},
	{"renderFrameStop", "()V", (void*) jni_player_render_frame_stop},

	{"getVideoDurationNative", "()J", (void*) jni_player_get_video_duration},
	{"render", "(Landroid/view/Surface;)V", (void*) jni_player_render},
};

#endif
