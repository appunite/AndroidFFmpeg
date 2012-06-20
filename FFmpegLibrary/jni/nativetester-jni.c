/*
 * nativetester-jni.c
 *
 *  Created on: Jun 19, 2012
 *      Author: Jacek Marchwicki <jacek.marchwicki@gmail.com>
 */

/*android specific headers*/
#include <jni.h>
#include <android/log.h>

#include <stdlib.h>
#include <assert.h>

#include "nativetester.h"

#ifndef NELEM
#define NELEM(x) ((int)(sizeof(x) / sizeof((x)[0])))
#endif


#define LOG_TAG "NativeTester-jni"
#define LOG_LEVEL 10
#define LOGI(level, ...) if (level <= LOG_LEVEL) {__android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__);}
#define LOGE(level, ...) if (level <= LOG_LEVEL) {__android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__);}

static int register_native_methods(JNIEnv* env,
		const char* class_name,
		JNINativeMethod* methods,
		int num_methods)
{
	jclass clazz;

	clazz = (*env)->FindClass(env, class_name);
	if (clazz == NULL) {
		LOGE(1, "Native registration unable to find class '%s'\n",
				class_name);
		return JNI_FALSE;
	}
	if ((*env)->RegisterNatives(env, clazz, methods, num_methods) < 0) {
		LOGE(1, "RegisterNatives failed for '%s'\n", class_name);
		return JNI_FALSE;
	}

	return JNI_TRUE;
}

jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved)
{
	JNIEnv* env = NULL;
	jint result = -1;

	if ((*vm)->GetEnv(vm, (void**) &env, JNI_VERSION_1_4) != JNI_OK) {
		LOGE(1, "ERROR: GetEnv failed\n");
		goto bail;
	}
	assert(env != NULL);

	if (register_native_methods(env,
			nativetester_class_path_name,
			nativetester_methods,
			NELEM(nativetester_methods)) < 0) {
		LOGE(1, "ERROR: Exif native registration failed\n");
		goto bail;
	}

	/* success -- return valid version number */
	result = JNI_VERSION_1_4;

bail:
	return result;
}
