/*
 * nativetester.h
 *
 *  Created on: Jun 19, 2012
 *      Author: Jacek Marchwicki <jacek.marchwicki@gmail.com>
 */

#ifndef NATIVETESTER_H_
#define NATIVETESTER_H_

static const char *nativetester_class_path_name = "com/appunite/ffmpeg/NativeTester";

jboolean jni_nativetester_is_neon(JNIEnv *env, jobject thiz);
jboolean jni_nativetester_is_vfpv3(JNIEnv *env, jobject thiz);


static JNINativeMethod nativetester_methods[] = {
		{"isNeon", "()B", (void*) jni_nativetester_is_neon},
		{"isVfpv3", "()B", (void*) jni_nativetester_is_vfpv3},
};

#endif /* NATIVETESTER_H_ */
