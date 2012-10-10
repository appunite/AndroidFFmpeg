#the yuv2rgb library
include $(CLEAR_VARS)

LOCAL_CFLAGS :=  -std=c99
#LOCAL_CFLAGS :=  -std=c99 -pedantic -v -c ';'

#disable thumb
#LOCAL_ARM_MODE := thumb
ifdef FEATURE_NEON
	LOCAL_ARM_NEON  := true
endif
LOCAL_CFLAGS := -O3
	
LOCAL_ALLOW_UNDEFINED_SYMBOLS=false
LOCAL_MODULE := yuv2rgb
ifeq ($(TARGET_ARCH_ABI),$(filter $(TARGET_ARCH_ABI),armeabi armeabi-v7a))
	LOCAL_SRC_FILES := yuv2rgb/yuv420rgb565.s yuv2rgb/yuv2rgb16tab.c yuv2rgb/nv12rgb565.s
else
	LOCAL_SRC_FILES := yuv2rgb/yuv420rgb565c.c yuv2rgb/yuv2rgb16tab.c yuv2rgb/nv12rgb565c.c
endif

#LOCAL_SRC_FILES := yuv2rgb/yuv2rgb16tab.c yuv2rgb/yuv420rgb8888.s yuv2rgb/yuv420rgb565.s 
#LOCAL_SRC_FILES := yuv2rgb16tab.c yuv420rgb8888.s yuv420rgb565.s yuv422rgb565.s yuv2rgb555.s yuv2rgbX.s yuv420rgb888.s yuv422rgb565.s yuv422rgb888.s yuv422rgb8888.s yuv444rgb565.s yuv444rgb888.s yuv444rgb8888.s

LOCAL_LDLIBS := -ldl -llog 

include $(BUILD_STATIC_LIBRARY)
