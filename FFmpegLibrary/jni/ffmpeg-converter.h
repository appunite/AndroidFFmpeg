/*
 * ffmpeg-converter.h
 *
 *  Created on: Mar 19, 2012
 *      Author: jacekmarchwicki
 */

#ifndef FFMPEG_CONVERTER_H_
#define FFMPEG_CONVERTER_H_

typedef struct VideoConverter VideoConverter;

void VideoConverter_free(VideoConverter * vc);
VideoConverter * VideoConverter_init();
void VideoConverter_register(VideoConverter *vc);
void VideoConverter_closeFile(VideoConverter *vc);
int VideoConverter_openFile(VideoConverter *vc, char *fileName);
void VideoConverter_dumpFormat(VideoConverter *vc);
int VideoConverter_findVideoStream(VideoConverter *vc);
int VideoConverter_findAudioStream(VideoConverter *vc);
int VideoConverter_findVideoCodec(VideoConverter *vc);
int VideoConverter_findAudioCodec(VideoConverter *vc);
void VideoConverter_closeAudioCodec(VideoConverter *vc);
void VideoConverter_closeVideoCodec(VideoConverter *vc);
void VideoConverter_createFrame(VideoConverter *vc);
void VideoConverter_freeFrame(VideoConverter *vc);
int VideoConverter_convertFrames(VideoConverter *vc);
int VideoConverter_readFrames(VideoConverter *vc);
int VideoConverter_openOutputFile(VideoConverter *vc, char * fileName);
void VideoConverter_closeOutputFile(VideoConverter *vc);
int VideoConverter_hasOutputVideoCodec(VideoConverter *vc);
int VideoConverter_hasOutputAudioCodec(VideoConverter *vc);
int VideoConverter_createVideoStream(VideoConverter *vc);
int VideoConverter_allocPicture(VideoConverter *vc);
int VideoConverter_openVideoStream(VideoConverter *vc);
void VideoConverter_closeVideoStream(VideoConverter *vc);
void VideoConverter_freeVideoStream(VideoConverter *vc);
int VideoConverter_createAudioStream(VideoConverter *vc);
int VideoConverter_openAudioStream(VideoConverter *vc);
void VideoConverter_closeAudioStream(VideoConverter *vc);
void VideoConverter_freeAudioStream(VideoConverter *vc);

#endif /* FFMPEG_CONVERTER_H_ */
