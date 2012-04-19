/**
this is the wrapper of the native functions 
from: http://www.roman10.net/how-to-port-ffmpeg-the-program-to-androidideas-and-thoughts/
**/
/*android specific headers*/
#include <jni.h>
#include <android/log.h>
/*standard library*/
#include <time.h>
#include <math.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <unistd.h>
#include <assert.h>
/*ffmpeg headers*/
#include <libavutil/avstring.h>
#include <libavutil/pixdesc.h>
#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>

#include <libavformat/avformat.h>

#include <libswscale/swscale.h>

#include <libavcodec/avcodec.h>
#include <libavcodec/opt.h>
#include <libavcodec/avfft.h>

#include "ffmpeg-converter.h"

/*for android logs*/
#define LOG_TAG "FFmpegTest"
#define LOG_LEVEL 10
#define LOGI(level, ...) if (level <= LOG_LEVEL) {__android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__);}
#define LOGE(level, ...) if (level <= LOG_LEVEL) {__android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__);}

AVFormatContext *gFormatCtx = NULL;
int gVideoStreamIndex;    //video stream index

AVCodecContext *gVideoCodecCtx = NULL;

/*parsing the video file, done by parse thread*/
static int get_video_info(char *gFilename) {

    LOGI(10, "getting video info from file: %s", gFilename);
    if (gFormatCtx != NULL)
    	return -6;
    AVCodec *lVideoCodec;
    int lError;
    /*some global variables initialization*/
    LOGI(10, "get video info starts!");
    /*register the codec*/
    av_register_all();
    LOGI(10, "registered all!");
    /*open the video file*/
    if ((lError = avformat_open_input(&gFormatCtx, gFilename, NULL, NULL)) < 0 ) {
        LOGE(1, "Error open video file: %d", lError);
        gFormatCtx = NULL;
        return -1;	//open file failed
    }
    LOGI(10, "Opened file")
    /*retrieve stream information*/
    if ((lError = av_find_stream_info(gFormatCtx)) < 0) {
        LOGE(1, "Error find stream information: %d", lError);
        av_close_input_file(gFormatCtx);
        gFormatCtx = NULL;
        return -2;
    } 
    LOGI(10, "found stream info")
    /*find the video stream and its decoder*/
    gVideoStreamIndex = av_find_best_stream(gFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, &lVideoCodec, 0);
    if (gVideoStreamIndex == AVERROR_STREAM_NOT_FOUND) {
        LOGE(1, "Error: cannot find a video stream");
        av_close_input_file(gFormatCtx);
        gFormatCtx = NULL;
        return -3;
    }

    LOGI(10, "video codec: %s", lVideoCodec->name);

    if (gVideoStreamIndex == AVERROR_DECODER_NOT_FOUND) {
        LOGE(1, "Error: video stream found, but no decoder is found!");
        av_close_input_file(gFormatCtx);
        gFormatCtx = NULL;
        return -4;
    }   
    /*open the codec*/
    gVideoCodecCtx = gFormatCtx->streams[gVideoStreamIndex]->codec;
    LOGI(10, "open codec: (%d, %d)", gVideoCodecCtx->height, gVideoCodecCtx->width);
#ifdef SELECTIVE_DECODING
    gVideoCodecCtx->allow_selective_decoding = 1;
#endif
    if (avcodec_open(gVideoCodecCtx, lVideoCodec) < 0) {
	LOGE(1, "Error: cannot open the video codec!");
		lVideoCodec = NULL;
		av_close_input_file(gFormatCtx);
		gFormatCtx = NULL;
        return -5;
    }
    LOGI(10, "get video info ends");
    return 0;
}

JNIEXPORT void JNICALL Java_com_appunite_ffmpeg_FFmpeg_naClose(JNIEnv *pEnv, jobject pObj) {
    int l_mbH = (gVideoCodecCtx->height + 15) / 16;
    /*close the video codec*/
    avcodec_close(gVideoCodecCtx);
    /*close the video file*/
    av_close_input_file(gFormatCtx);
    gFormatCtx = NULL;
    gVideoCodecCtx = NULL;
}

JNIEXPORT jint JNICALL Java_com_appunite_ffmpeg_FFmpeg_naInit(JNIEnv *pEnv, jobject pObj, jstring pFileName) {
    int l_mbH, l_mbW;
    /*get the video file name*/
    char *gFileName = (char *)(*pEnv)->GetStringUTFChars(pEnv, pFileName, NULL);
    if (gFileName == NULL) {
        LOGE(1, "Error: cannot get the video file name!");
        return -1;
    } 
    LOGI(10, "video file name is %s", gFileName);
    int code = get_video_info(gFileName);
    LOGI(10, "initialization done with status code: %d", code);
    (*pEnv)->ReleaseStringUTFChars(pEnv, pFileName, gFileName);
    return code;
}

JNIEXPORT jstring JNICALL Java_com_appunite_ffmpeg_FFmpeg_naGetVideoCodecName(JNIEnv *pEnv, jobject pObj) {
	if (gVideoCodecCtx == NULL) {
		LOGE(1, "format context not created");
		return NULL;
	}
    const char* lCodecName = gVideoCodecCtx->codec->name;
    if (lCodecName == NULL)
    	return NULL;
    return (*pEnv)->NewStringUTF(pEnv, lCodecName);
}

JNIEXPORT jstring JNICALL Java_com_appunite_ffmpeg_FFmpeg_naGetVideoFormatName(JNIEnv *pEnv, jobject pObj) {
	if (gFormatCtx == NULL) {
		LOGE(1, "format context not created");
		return NULL;
	}
    const char* lFormatName = gFormatCtx->iformat->name;
    if (lFormatName == NULL)
    	return NULL;
    return (*pEnv)->NewStringUTF(pEnv, lFormatName);
}


JNIEXPORT jintArray JNICALL Java_com_appunite_ffmpeg_FFmpeg_naGetVideoResolution(JNIEnv *pEnv, jobject pObj) {
    jintArray lRes;
    if (gVideoCodecCtx == NULL) {
    	LOGE(1, "codec context not created");
    	return NULL;
    }
    lRes = (*pEnv)->NewIntArray(pEnv, 2);
    if (lRes == NULL) {
        LOGE(1, "cannot allocate memory for video size");
        return NULL;
    }
    jint lVideoRes[2];
    lVideoRes[0] = gVideoCodecCtx->width;
    lVideoRes[1] = gVideoCodecCtx->height;
    (*pEnv)->SetIntArrayRegion(pEnv, lRes, 0, 2, lVideoRes);
    return lRes;
}

void convertToMov(VideoConverter *vc, char *outputFile) {
	int err;
	err = VideoConverter_openOutputFile(vc, outputFile);
	if (err >= 0) {
		LOGI(10, "Opened output file VideoConverter");
		int hasOutputVideoCodec = VideoConverter_hasOutputVideoCodec(vc);
		int hasOutputAudioCodec = VideoConverter_hasOutputAudioCodec(vc);
		if (hasOutputAudioCodec && hasOutputVideoCodec) {
			LOGI(10, "Has codecs VideoConverter");
			err = VideoConverter_createVideoStream(vc);
			if (err >= 0) {
				LOGI(10, "Created video stream VideoConverter");
				err = VideoConverter_openVideoStream(vc);
				if (err >= 0) {
					LOGI(10, "Opened video stream VideoConverter");
					err = VideoConverter_createAudioStream(vc);
					if (err >= 0) {
						LOGI(10, "Created audio stream VideoConverter");
						err = VideoConverter_openAudioStream(vc);
						if (err >= 0) {
							LOGI(10, "Opened audio stream VideoConverter");
							LOGI(10, "Doing the work");
							VideoConverter_convertFrames(vc);
							LOGI(10, "Done work");
							VideoConverter_closeAudioStream(vc);
						} else {
							LOGE(1, "Could not open audio stream");
						}
						VideoConverter_freeAudioStream(vc);
					} else {
						LOGE(1, "Could not create audio stream");
					}
					VideoConverter_closeVideoStream(vc);
				} else {
					LOGE(1, "Could not open video stream");
				}
				VideoConverter_freeVideoStream(vc);
			} else {
				LOGE(1, "Could not create video stream");
			}
		}
		if (!hasOutputAudioCodec) {
			VideoConverter_printAllCodecs();
			LOGE(1, "File does not have audio codec");
		}
		if (!hasOutputVideoCodec) {
			VideoConverter_printAllCodecs();
			LOGE(1, "File does not have video codec");
		}
		VideoConverter_closeOutputFile(vc);
	} else {
		LOGE(1, "Could not open output file: %s", outputFile);
	}
}

void convertToMpeg(VideoConverter *vc, char *outputFile) {
	int err;
	err = VideoConverter_openOutputFile(vc, outputFile);
	if (err >= 0) {
		LOGI(10, "Opened output file VideoConverter");
		int hasOutputVideoCodec = VideoConverter_hasOutputVideoCodec(vc);
		int hasOutputAudioCodec = VideoConverter_hasOutputAudioCodec(vc);
		if (hasOutputAudioCodec && hasOutputVideoCodec) {
			LOGI(10, "Has codecs VideoConverter");
			err = VideoConverter_createVideoStream(vc);
			if (err >= 0) {
				LOGI(10, "Created video stream VideoConverter");
				err = VideoConverter_openVideoStream(vc);
				if (err >= 0) {
					LOGI(10, "Opened video stream VideoConverter");
					err = VideoConverter_createAudioStream(vc);
					if (err >= 0) {
						LOGI(10, "Created audio stream VideoConverter");
						err = VideoConverter_openAudioStream(vc);
						if (err >= 0) {
							LOGI(10, "Opened audio stream VideoConverter");
							LOGI(10, "Doing the work");
							VideoConverter_convertFrames(vc);
							LOGI(10, "Done work");
							VideoConverter_closeAudioStream(vc);
						} else {
							LOGE(1, "Could not open audio stream");
						}
						VideoConverter_freeAudioStream(vc);
					} else {
						LOGE(1, "Could not create audio stream");
					}
					VideoConverter_closeVideoStream(vc);
				} else {
					LOGE(1, "Could not open video stream");
				}
				VideoConverter_freeVideoStream(vc);
			} else {
				LOGE(1, "Could not create video stream");
			}
		}
		if (!hasOutputAudioCodec) {
			VideoConverter_printAllCodecs();
			LOGE(1, "File does not have audio codec");
		}
		if (!hasOutputVideoCodec) {
			VideoConverter_printAllCodecs();
			LOGE(1, "File does not have video codec");
		}
		VideoConverter_closeOutputFile(vc);
	} else {
		LOGE(1, "Could not open output file: %s", outputFile);
	}
}

JNIEXPORT jint JNICALL Java_com_appunite_ffmpeg_FFmpeg_naConvertMovToMpeg(JNIEnv *pEnv, jobject pObj, jstring jInputFile, jstring jOutputFile) {
	char *inputFile = (char *)(*pEnv)->GetStringUTFChars(pEnv, jInputFile, NULL);
	char *outputFile = (char *)(*pEnv)->GetStringUTFChars(pEnv, jOutputFile, NULL);

	int err;
	VideoConverter *vc = VideoConverter_init();
	VideoConverter_register(vc);
	LOGI(10, "Initialized VideoConverter");
	VideoConverter_printAllCodecs();
	err = VideoConverter_openFile(vc, inputFile);
	if (err >= 0) {
		LOGI(10, "Opened VideoConverter");
		VideoConverter_dumpFormat(vc);
		err = VideoConverter_findVideoStream(vc);
		if (err >= 0) {
			LOGI(10, "Found video stream VideoConverter");
			err = VideoConverter_findAudioStream(vc);
			if (err >= 0) {
				LOGI(10, "Found audio stream VideoConverter");
				err = VideoConverter_findVideoCodec(vc);
				if (err >= 0) {
					LOGI(10, "Found video codec VideoConverter");
					err = VideoConverter_findAudioCodec(vc);
					if (err >= 0) {
						LOGI(10, "Found audio codec VideoConverter");
						VideoConverter_createFrame(vc);

						convertToMpeg(vc, outputFile);
						//VideoConverter_readFrames(vc); // work

						VideoConverter_freeFrame(vc);
						VideoConverter_closeAudioCodec(vc);
					} else {
						LOGE(1, "Could not find audio codec");
					}
					VideoConverter_closeVideoCodec(vc);
				} else {
					LOGE(1, "Could not find video codec");
				}
			} else {
				LOGE(1, "Could not find audio stream");
			}
		} else {
			LOGE(1, "Could not find video stream");
		}
		VideoConverter_closeFile(vc);
	} else {
		LOGE(1, "Could not open video file");
	}
	VideoConverter_free(vc);
	return 0;
}

JNIEXPORT jint JNICALL Java_com_appunite_ffmpeg_FFmpeg_naConvertMpegToMov(JNIEnv *pEnv, jobject pObj, jstring jInputFile, jstring jOutputFile) {
	char *inputFile = (char *)(*pEnv)->GetStringUTFChars(pEnv, jInputFile, NULL);
	char *outputFile = (char *)(*pEnv)->GetStringUTFChars(pEnv, jOutputFile, NULL);

	int err;
	VideoConverter *vc = VideoConverter_init();
	VideoConverter_register(vc);
	LOGI(10, "Initialized VideoConverter");
	VideoConverter_printAllCodecs();
	err = VideoConverter_openFile(vc, inputFile);
	if (err >= 0) {
		LOGI(10, "Opened VideoConverter");
		VideoConverter_dumpFormat(vc);
		err = VideoConverter_findVideoStream(vc);
		if (err >= 0) {
			LOGI(10, "Found video stream VideoConverter");
			err = VideoConverter_findAudioStream(vc);
			if (err >= 0) {
				LOGI(10, "Found audio stream VideoConverter");
				err = VideoConverter_findVideoCodec(vc);
				if (err >= 0) {
					LOGI(10, "Found video codec VideoConverter");
					err = VideoConverter_findAudioCodec(vc);
					if (err >= 0) {
						LOGI(10, "Found audio codec VideoConverter");
						VideoConverter_createFrame(vc);

						convertToMov(vc, outputFile);
						//VideoConverter_readFrames(vc); // work

						VideoConverter_freeFrame(vc);
						VideoConverter_closeAudioCodec(vc);
					} else {
						LOGE(1, "Could not find audio codec");
					}
					VideoConverter_closeVideoCodec(vc);
				} else {
					LOGE(1, "Could not find video codec");
				}
			} else {
				LOGE(1, "Could not find audio stream");
			}
		} else {
			LOGE(1, "Could not find video stream");
		}
		VideoConverter_closeFile(vc);
	} else {
		LOGE(1, "Could not open video file");
	}
	VideoConverter_free(vc);
	return 0;
}


