LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := ffmpeg-prebuilt
LOCAL_SRC_FILES := ffmpeg-build/$(TARGET_ARCH_ABI)/libffmpeg.so
LOCAL_EXPORT_C_INCLUDES := ffmpeg-build/$(TARGET_ARCH_ABI)/include
LOCAL_EXPORT_LDLIBS := ffmpeg-build//$(TARGET_ARCH_ABI)/libffmpeg.so
LOCAL_PRELINK_MODULE := true
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_ALLOW_UNDEFINED_SYMBOLS=false
LOCAL_MODULE := ffmpeg-jni
LOCAL_SRC_FILES := ffmpeg-jni.c ffmpeg-converter.c
LOCAL_C_INCLUDES := $(LOCAL_PATH)/ffmpeg-build/$(TARGET_ARCH_ABI)/include
LOCAL_SHARED_LIBRARY := ffmpeg-prebuilt
#LOCAL_LDLIBS    := -llog -ljnigraphics -lz -lm $(LOCAL_PATH)/ffmpeg-build/$(TARGET_ARCH_ABI)/libffmpeg.so
LOCAL_LDLIBS    := -llog -lz -lm $(LOCAL_PATH)/ffmpeg-build/$(TARGET_ARCH_ABI)/libffmpeg.so
include $(BUILD_SHARED_LIBRARY)
