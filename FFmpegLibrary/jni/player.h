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

static char *player_class_path_name = "com/appunite/ffmpeg/FFmpegPlayer";
static char *android_track_class_path_name = "android/media/AudioTrack";
static char *interrupted_exception_class_path_name = "java/lang/InterruptedException";
static char *runtime_exception_class_path_name = "java/lang/RuntimeException";
static char *not_playing_exception_class_path_name = "com/appunite/ffmpeg/NotPlayingException";

// FFmpegPlayer
static JavaField player_m_native_player = {"mNativePlayer", "I"};
static JavaMethod player_on_update_time = {"onUpdateTime","(II)V"};
static JavaMethod player_prepare_audio_track = {"prepareAudioTrack", "(II)Landroid/media/AudioTrack;"};
static JavaMethod player_prepare_frame = {"prepareFrame", "(II)Landroid/graphics/Bitmap;"};

// AudioTrack
static JavaMethod audio_track_write = {"write", "([BII)I"};
static JavaMethod audio_track_pause = {"pause", "()V"};
static JavaMethod audio_track_play = {"play", "()V"};
static JavaMethod audio_track_stop = {"stop", "()V"};

// Player

int jni_player_init(JNIEnv *env, jobject thiz);
void jni_player_dealloc(JNIEnv *env, jobject thiz);

void jni_player_pause(JNIEnv *env, jobject thiz);
void jni_player_resume(JNIEnv *env, jobject thiz);

int jni_player_set_data_source (JNIEnv *env, jobject thiz, jstring string);
int jni_player_stop(JNIEnv *env, jobject thiz);

void jni_player_render_frame_start(JNIEnv *env, jobject thiz);
void jni_player_render_frame_stop(JNIEnv *env, jobject thiz);
jobject jni_player_render_frame(JNIEnv *env, jobject thiz);
void jni_player_release_frame (JNIEnv *env, jobject thiz);

int jni_player_get_video_duration(JNIEnv *env, jobject thiz);

static JNINativeMethod player_methods[] = {

	{"initNative", "()I", (void*) jni_player_init},
	{"deallocNative", "()V", (void*) jni_player_dealloc},

	{"pauseNative", "()V", (void*) jni_player_pause},
	{"resumeNative", "()V", (void*) jni_player_resume},

	{"setDataSourceNative", "(Ljava/lang/String;)I", (void*) jni_player_set_data_source},
	{"stopNative", "()I", (void*) jni_player_stop},

	{"renderFrameStart", "()V", (void*) jni_player_render_frame_start},
	{"renderFrameStop", "()V", (void*) jni_player_render_frame_stop},
	{"renderFrameNative", "()Landroid/graphics/Bitmap;", (void*) jni_player_render_frame},
	{"releaseFrame", "()V", (void*) jni_player_release_frame},

	{"getVideoDurationNative", "()I", (void*) jni_player_get_video_duration},
};

#endif
