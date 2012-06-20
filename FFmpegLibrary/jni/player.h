/*
 * player.h
 *
 *  Created on: Mar 19, 2012
 *      Author: Jacek Marchwicki <jacek.marchwicki@gmail.com>
 */

#ifndef H_PLAYER
#define H_PLAYER

static const char *player_class_path_name = "com/appunite/ffmpeg/FFmpegPlayer";
static const char *android_track_class_path_name = "android/media/AudioTrack";

int jni_player_set_data_source (JNIEnv *env, jobject thiz, jstring string);
int jni_player_play(JNIEnv *env, jobject thiz);
int jni_player_decode_video(JNIEnv *env, jobject thiz);
int jni_player_decode_audio(JNIEnv *env, jobject thiz);
void jni_player_stop(JNIEnv *env, jobject thiz);

jobject jni_player_render_frame(JNIEnv *env, jobject thiz);
void jni_player_release_frame (JNIEnv *env, jobject thiz);

static JNINativeMethod player_methods[] = {
	{"setDataSourceNative", "(Ljava/lang/String;)I", (void*) jni_player_set_data_source},
	{"playNative", "()I", (void*) jni_player_play},
	{"decodeVideoNative", "()I", (void*) jni_player_decode_video},
	{"decodeAudioNative", "()I", (void*) jni_player_decode_audio},
	{"renderFrameNative", "()Landroid/graphics/Bitmap;", (void*) jni_player_render_frame},
	{"releaseFrameNative", "()V", (void*) jni_player_release_frame},
	{"stopNative", "()V", (void*) jni_player_stop},
};

#endif
