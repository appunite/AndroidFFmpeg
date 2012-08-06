/*
 * jni-protocol.c
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

#include <libavutil/avstring.h>
#include <libavformat/avformat.h>
#include <jni.h>

#include "ffmpeg/libavformat/url.h"

#include "jni-protocol.h"

static const char *jni_reader_class_name = "com/appunite/ffmpeg/JniReader";
static JavaVM *global_jvm;

static int jni_read(URLContext *h, unsigned char *buf, int size) {
	int err = 0;
	JNIEnv* env;
	jclass jni_reader_class;
	jmethodID jni_reader_read;
	jobject jni_reader;
	jbyteArray byte_array;
	jbyte *jni_samples;
	if ((*global_jvm)->GetEnv(global_jvm, (void**) &env, JNI_VERSION_1_4)) {
		err = -1;
		goto end;
	}
	if (env == NULL) {
		err = -1;
		goto end;
	}

	jni_reader_class = (*env)->FindClass(env, jni_reader_class_name);
	if (jni_reader_class == NULL) {
		err = -1;
		goto end;
	}

	jni_reader_read = (*env)->GetMethodID(env, jni_reader_class, "read",
			"([B)I");
	if (jni_reader_read == NULL) {
		err = -1;
		goto end;
	}

	jni_reader = (jobject) h->priv_data;

	byte_array = (*env)->NewByteArray(env, size);

	err = (*env)->CallIntMethod(env, jni_reader, jni_reader_read, byte_array);

	jni_samples = (*env)->GetByteArrayElements(env, byte_array, NULL);
	memcpy(buf, jni_samples, size);
	(*env)->ReleaseByteArrayElements(env, byte_array, jni_samples, 0);

	(*env)->DeleteLocalRef(env, byte_array);

	end: return err >= 0 ? err : AVERROR(err);
}

static int jni_write(URLContext *h, const unsigned char *buf, int size) {
	int err = 0;
	JNIEnv* env;
	jclass jni_reader_class;
	jmethodID jni_reader_write;
	jobject jni_reader;
	jbyteArray byte_array;
	jbyte *jni_samples;
	if ((*global_jvm)->GetEnv(global_jvm, (void**) &env, JNI_VERSION_1_4)) {
		err = -1;
		goto end;
	}
	if (env == NULL) {
		err = -1;
		goto end;
	}

	jni_reader_class = (*env)->FindClass(env, jni_reader_class_name);
	if (jni_reader_class == NULL) {
		err = -1;
		goto end;
	}

	jni_reader_write = (*env)->GetMethodID(env, jni_reader_class, "write",
			"([B)I");
	if (jni_reader_write == NULL) {
		err = -1;
		goto end;
	}

	jni_reader = (jobject) h->priv_data;

	byte_array = (*env)->NewByteArray(env, size);

	jni_samples = (*env)->GetByteArrayElements(env, byte_array, NULL);
	memcpy(jni_samples, buf, size);
	(*env)->ReleaseByteArrayElements(env, byte_array, jni_samples, 0);

	err = (*env)->CallIntMethod(env, jni_reader, jni_reader_write, byte_array);

	(*env)->DeleteLocalRef(env, byte_array);

	end: return err >= 0 ? err : AVERROR(err);
}

static int jni_get_handle(URLContext *h) {
	return (intptr_t) h->priv_data;
}

static int jni_check(URLContext *h, int mask) {
	int err = 0;
	JNIEnv* env;
	jclass jni_reader_class;
	jmethodID jni_reader_check;
	jobject jni_reader;

	if ((*global_jvm)->GetEnv(global_jvm, (void**) &env, JNI_VERSION_1_4)) {
		err = -1;
		goto end;
	}
	if (env == NULL) {
		err = -1;
		goto end;
	}

	jni_reader_class = (*env)->FindClass(env, jni_reader_class_name);
	if (jni_reader_class == NULL) {
		err = -1;
		goto end;
	}

	jni_reader_check = (*env)->GetMethodID(env, jni_reader_class, "check",
			"(I)I");
	if (jni_reader_check == NULL) {
		err = -1;
		goto end;
	}

	jni_reader = (jobject) h->priv_data;

	err = (*env)->CallIntMethod(env, jni_reader, jni_reader_check, mask);

	end: return err >= 0 ? err : AVERROR(err);
}

static int jni_open2(URLContext *h, const char *url, int flags,
		AVDictionary **options) {
	int err = 0;
	JNIEnv* env;
	jclass jni_reader_class;
	jmethodID jni_reader_constructor;
	jstring url_java_string;
	jobject jni_reader;

	if ((*global_jvm)->GetEnv(global_jvm, (void**) &env, JNI_VERSION_1_4)) {
		err = -1;
		goto end;
	}
	if (env == NULL) {
		err = -1;
		goto end;
	}

	jni_reader_class = (*env)->FindClass(env, jni_reader_class_name);
	if (jni_reader_class == NULL) {
		err = -1;
		goto end;
	}

	jni_reader_constructor = (*env)->GetMethodID(env, jni_reader_class,
			"<init>", "(Ljava/lang/String;I)V");
	if (jni_reader_constructor == NULL) {
		err = -1;
		goto end;
	}

	url_java_string = (*env)->NewStringUTF(env, url);

	if (url_java_string == NULL) {
		err = -1;
		goto end;
	}

	jni_reader = (*env)->NewObject(env, jni_reader_class,
			jni_reader_constructor, url_java_string, flags);
	if (jni_reader == NULL) {
		err = -1;
		goto free_url_java_string;
	}

	h->priv_data = (void *) (*env)->NewGlobalRef(env, jni_reader);
	if (h->priv_data == NULL) {
		err = -1;
		goto free_jni_reader;
	}

	free_jni_reader:

	(*env)->DeleteLocalRef(env, jni_reader);

	free_url_java_string:

	(*env)->DeleteLocalRef(env, url_java_string);

	end: return err >= 0 ? err : AVERROR(err);
}

static int jni_open(URLContext *h, const char *filename, int flags) {
	return jni_open2(h, filename, flags, NULL);
}

static int64_t jni_seek(URLContext *h, int64_t pos, int whence) {
	int64_t err = 0;
	JNIEnv* env;
	jclass jni_reader_class;
	jmethodID jni_reader_seek;
	jobject jni_reader;

	if ((*global_jvm)->GetEnv(global_jvm, (void**) &env, JNI_VERSION_1_4)) {
		err = -1;
		goto end;
	}
	if (env == NULL) {
		err = -1;
		goto end;
	}

	jni_reader_class = (*env)->FindClass(env, jni_reader_class_name);
	if (jni_reader_class == NULL) {
		err = -1;
		goto end;
	}

	jni_reader_seek = (*env)->GetMethodID(env, jni_reader_class, "seek",
			"(JI)J");
	if (jni_reader_seek == NULL) {
		err = -1;
		goto end;
	}

	jni_reader = (jobject) h->priv_data;

	err = (*env)->CallIntMethod(env, jni_reader, jni_reader_seek, pos, whence);

	end: return err >= 0 ? err : AVERROR(err);
}

static int jni_close(URLContext *h) {
	int err = 0;
	JNIEnv* env;
	jobject jni_reader;

	if ((*global_jvm)->GetEnv(global_jvm, (void**) &env, JNI_VERSION_1_4)) {
		err = -1;
		goto end;
	}
	if (env == NULL) {
		err = -1;
		goto end;
	}

	jni_reader = (jobject) h->priv_data;

	(*env)->DeleteGlobalRef(env, jni_reader);

	end: return err >= 0 ? err : AVERROR(err);
}

URLProtocol jni_protocol = { .name = "jni", .url_open2 = jni_open2,
		.url_open = jni_open, .url_read = jni_read, .url_write = jni_write,
		.url_seek = jni_seek, .url_close = jni_close, .url_get_file_handle =
				jni_get_handle, .url_check = jni_check, };

void register_jni_protocol(JavaVM *jvm) {
	global_jvm = jvm;
	ffurl_register_protocol(&jni_protocol, sizeof(jni_protocol));
}
