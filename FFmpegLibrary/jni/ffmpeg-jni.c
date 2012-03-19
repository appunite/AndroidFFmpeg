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

/**/
char *gFileName;	  //the file name of the video

AVFormatContext *gFormatCtx;
int gVideoStreamIndex;    //video stream index

AVCodecContext *gVideoCodecCtx;

static int get_video_info(char *prFilename);

/*parsing the video file, done by parse thread*/
static int get_video_info(char *prFilename) {
    AVCodec *lVideoCodec;
    int lError;
    /*some global variables initialization*/
    LOGI(10, "get video info starts!");
    /*register the codec*/
    extern AVCodec ff_h263_decoder;
    avcodec_register(&ff_h263_decoder);
    extern AVCodec ff_h264_decoder;
    avcodec_register(&ff_h264_decoder);
    extern AVCodec ff_mpeg4_decoder;
    avcodec_register(&ff_mpeg4_decoder);
    extern AVCodec ff_mjpeg_decoder;
    avcodec_register(&ff_mjpeg_decoder);
    /*register parsers*/
    //extern AVCodecParser ff_h264_parser;
    //av_register_codec_parser(&ff_h264_parser);
    //extern AVCodecParser ff_mpeg4video_parser;
    //av_register_codec_parser(&ff_mpeg4video_parser);
    /*register demux*/
    extern AVInputFormat ff_mov_demuxer;
    av_register_input_format(&ff_mov_demuxer);
    //extern AVInputFormat ff_h264_demuxer;
    //av_register_input_format(&ff_h264_demuxer);
    /*register the protocol*/
    extern URLProtocol ff_file_protocol;
    av_register_protocol2(&ff_file_protocol, sizeof(ff_file_protocol));
    /*open the video file*/
    if ((lError = av_open_input_file(&gFormatCtx, gFileName, NULL, 0, NULL)) !=0 ) {
        LOGE(1, "Error open video file: %d", lError);
        return -1;	//open file failed
    }
    /*retrieve stream information*/
    if ((lError = av_find_stream_info(gFormatCtx)) < 0) {
        LOGE(1, "Error find stream information: %d", lError);
        return -2;
    } 
    /*find the video stream and its decoder*/
    gVideoStreamIndex = av_find_best_stream(gFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, &lVideoCodec, 0);
    if (gVideoStreamIndex == AVERROR_STREAM_NOT_FOUND) {
        LOGE(1, "Error: cannot find a video stream");
        return -3;
    } else {
	LOGI(10, "video codec: %s", lVideoCodec->name);
    }
    if (gVideoStreamIndex == AVERROR_DECODER_NOT_FOUND) {
        LOGE(1, "Error: video stream found, but no decoder is found!");
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
}

JNIEXPORT jint JNICALL Java_com_appunite_ffmpeg_FFmpeg_naInit(JNIEnv *pEnv, jobject pObj, jstring pFileName) {
    int l_mbH, l_mbW;
    /*get the video file name*/
    gFileName = (char *)(*pEnv)->GetStringUTFChars(pEnv, pFileName, NULL);
    if (gFileName == NULL) {
        LOGE(1, "Error: cannot get the video file name!");
        return -1;
    } 
    LOGI(10, "video file name is %s", gFileName);
    int code = get_video_info(gFileName);
    LOGI(10, "initialization done with status code: %d", code);
    return code;
}

JNIEXPORT jstring JNICALL Java_com_appunite_ffmpeg_FFmpeg_naGetVideoCodecName(JNIEnv *pEnv, jobject pObj) {
    char* lCodecName = gVideoCodecCtx->codec->name;
    return (*pEnv)->NewStringUTF(pEnv, lCodecName);
}

JNIEXPORT jstring JNICALL Java_com_appunite_ffmpeg_FFmpeg_naGetVideoFormatName(JNIEnv *pEnv, jobject pObj) {
    char* lFormatName = gFormatCtx->iformat->name;
    return (*pEnv)->NewStringUTF(pEnv, lFormatName);
}


JNIEXPORT jintArray JNICALL Java_com_appunite_ffmpeg_FFmpeg_naGetVideoResolution(JNIEnv *pEnv, jobject pObj) {
    jintArray lRes;
    lRes = (*pEnv)->NewIntArray(pEnv, 2);
    if (lRes == NULL) {
        LOGI(1, "cannot allocate memory for video size");
        return NULL;
    }
    jint lVideoRes[2];
    lVideoRes[0] = gVideoCodecCtx->width;
    lVideoRes[1] = gVideoCodecCtx->height;
    (*pEnv)->SetIntArrayRegion(pEnv, lRes, 0, 2, lVideoRes);
    return lRes;
}

void convertToFile(VideoConverter *vc, char *outputFile) {
	int err;
	err = VideoConverter_openOutputFile(vc, outputFile);
	if (err >= 0) {
		int hasOutputVideoCodec = VideoConverter_hasOutputVideoCodec(vc);
		int hasOutputAudioCodec = VideoConverter_hasOutputAudioCodec(vc);
		if (hasOutputAudioCodec && hasOutputVideoCodec) {
			err = VideoConverter_createVideoStream(vc);
			if (err >= 0) {
				err = VideoConverter_openVideoStream(vc);
				if (err >= 0) {
					err = VideoConverter_createAudioStream(vc);
					if (err >= 0) {
						err = VideoConverter_openAudioStream(vc);
						if (err >= 0) {
							printf("Doing the work\n");
							VideoConverter_convertFrames(vc);
							printf("Done work\n");
							VideoConverter_closeAudioStream(vc);
						} else {
							fprintf(stderr, "Could not open audio stream\n");
						}
						VideoConverter_freeAudioStream(vc);
					} else {
						fprintf(stderr, "Could not create audio stream\n");
					}
					VideoConverter_closeVideoStream(vc);
				} else {
					fprintf(stderr, "Could not open video stream\n");
				}
				VideoConverter_freeVideoStream(vc);
			} else {
				fprintf(stderr, "Could not create video stream\n");
			}
		}
		if (!hasOutputAudioCodec) {
			fprintf(stderr, "File does not have audio codec\n");
		}
		if (!hasOutputVideoCodec) {
			fprintf(stderr, "File does not have video codec\n");
		}
		VideoConverter_closeOutputFile(vc);
	} else {
		fprintf(stderr, "Could not open output file: %s\n", outputFile);
	}
}

JNIEXPORT jint JNICALL Java_com_appunite_ffmpeg_FFmpeg_naConvert(JNIEnv *pEnv, jobject pObj, jstring jInputFile, jstring jOutputFile) {
	char *inputFile = (char *)(*pEnv)->GetStringUTFChars(pEnv, jInputFile, NULL);
	char *outputFile = (char *)(*pEnv)->GetStringUTFChars(pEnv, jOutputFile, NULL);

	int err;
	VideoConverter *vc = VideoConverter_init();
	VideoConverter_register(vc);
	err = VideoConverter_openFile(vc, inputFile);
	if (err >= 0) {
		VideoConverter_dumpFormat(vc);
		err = VideoConverter_findVideoStream(vc);
		if (err >= 0) {
			err = VideoConverter_findAudioStream(vc);
			if (err >= 0) {
				err = VideoConverter_findVideoCodec(vc);
				if (err >= 0) {
					err = VideoConverter_findAudioCodec(vc);
					if (err >= 0) {
						VideoConverter_createFrame(vc);

						convertToFile(vc, outputFile);
						//VideoConverter_readFrames(vc); // work

						VideoConverter_freeFrame(vc);
						VideoConverter_closeAudioCodec(vc);
					} else {
						fprintf(stderr, "Could not find audio codec\n");
					}
					VideoConverter_closeVideoCodec(vc);
				} else {
					fprintf(stderr, "Could not find video codec\n");
				}
			} else {
				fprintf(stderr, "Could not find audio stream\n");
			}
		} else {
			fprintf(stderr, "Could not find video stream\n");
		}
		VideoConverter_closeFile(vc);
	} else {
		fprintf(stderr, "Could not open video file");
	}
	VideoConverter_free(vc);
	return 0;
}


