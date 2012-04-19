#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#include <libavutil/avstring.h>
#include <libavutil/pixdesc.h>
#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>

#include <libswscale/swscale.h>

#include <libavcodec/opt.h>
#include <libavcodec/avfft.h>

#include <android/log.h>

#include "ffmpeg-converter.h"

#define LOG_LEVEL 10
#define LOG_TAG "FFmpegTest"
#define LOGI(level, ...) if (level <= LOG_LEVEL) {__android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__);}
#define LOGE(level, ...) if (level <= LOG_LEVEL) {__android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__);}

#define FALSE 0
#define TRUE (!(FALSE))

/* 5 seconds stream duration */
#define STREAM_FRAME_RATE 30 /* 25 images/s */
#define STREAM_PIX_FMT PIX_FMT_YUV420P /* default pix_fmt */

struct VideoConverter {
	AVFormatContext *inputFormatCtx;
	char *inputFileName;
	int inputVideoStreamNumber;
	int inputAudioStreamNumber;
	AVCodecContext *inputVideoCodecCtx;
	AVCodecContext *inputAudioCodecCtx;
	AVCodec *inputVideoCodec;
	AVCodec *inputAudioCodec;
	AVFrame *inputVideoFrame;
	AVStream *inputAudioStream;
	AVStream *inputVideoStream;

	AVOutputFormat *outputFmt;
	AVStream *outputAudioStream;
	AVStream *outputVideoStream;

	AVCodecContext *outputVideoCodecCtx;
	AVCodecContext *outputAudioCodecCtx;
	AVCodec *outputVideoCodec;
	AVCodec *outputAudioCodec;

	uint8_t *outputVideoBuf;
	int outputVideoBufSize;

	uint8_t *outputAudioBuf;
	int outputAudioBufSize;
	char * outputFileName;
	AVFormatContext *outputFormatCtx;
};

void VideoConverter_free(VideoConverter * vc) {
	free(vc);
}

VideoConverter * VideoConverter_init() {
	VideoConverter * this = malloc(sizeof(VideoConverter));
	this->inputFormatCtx = NULL;
	this->inputFileName = NULL;
	this->inputVideoStreamNumber = -1;
	this->inputAudioStreamNumber = -1;
	this->inputVideoCodecCtx = NULL;
	this->inputAudioCodecCtx = NULL;
	this->inputVideoCodec = NULL;
	this->inputAudioCodec = NULL;
	this->inputVideoFrame = NULL;
	return this;
}

void VideoConverter_register(VideoConverter *vc) {
	printf("Loaded all codecs\n");

	av_register_all();
}

void VideoConverter_closeFile(VideoConverter *vc) {
	//av_close_input_file(vc->inputFormatCtx);
	//avformat_free_context(vc->inputFormatCtx);
	av_free(vc->inputFormatCtx);
	vc->inputFormatCtx = NULL;
	vc->inputFileName = NULL;
	vc->inputVideoStreamNumber = -1;
	vc->inputAudioStreamNumber = -1;
	vc->inputVideoCodecCtx = NULL;
	vc->inputAudioCodecCtx = NULL;
}

int VideoConverter_openFile(VideoConverter *vc, char *fileName) {
	vc->inputFormatCtx = avformat_alloc_context();
	if (vc->inputFormatCtx == NULL) {
		fprintf(stderr, "Could not create AVContext\n");
		return -2;
	}

	vc->inputFileName = fileName;

	printf("Opening file: %s\n", fileName);
	if (av_open_input_file(&(vc->inputFormatCtx), fileName, NULL, 0, NULL)
			!= 0) {
		avformat_free_context(vc->inputFormatCtx);
		vc->inputFormatCtx = NULL;
		fprintf(stderr, "Could not open video file: %s\n", fileName);
		return -1;
	}
	printf("Opened video file\n");
	if (av_find_stream_info(vc->inputFormatCtx) < 0) {
		VideoConverter_closeFile(vc);
		fprintf(stderr, "Could not open stream\n");
		return -2;
	}
	printf("Opened stream\n");
	return 0;
}

void VideoConverter_dumpFormat(VideoConverter *vc) {
	av_dump_format(vc->inputFormatCtx, 0, vc->inputFileName, FALSE);
}

int VideoConverter_findVideoStream(VideoConverter *vc) {
	int videoStream = -1;
	int i;
	for (i = 0; i < vc->inputFormatCtx->nb_streams; i++) {
		AVStream *stream = vc->inputFormatCtx->streams[i];
		AVCodecContext *codec = stream->codec;
		if (codec->codec_type == AVMEDIA_TYPE_VIDEO) {
			videoStream = i;
			break;
		}
	}
	if (videoStream < 0) {
		return -1;
	}
	printf("Found video stream at: %d\n", videoStream);
	vc->inputVideoStream = vc->inputFormatCtx->streams[videoStream];
	vc->inputVideoCodecCtx = vc->inputVideoStream->codec;
	vc->inputVideoStreamNumber = videoStream;
	return 0;
}

int VideoConverter_findAudioStream(VideoConverter *vc) {
	int audioStream = -1;
	int i;
	for (i = 0; i < vc->inputFormatCtx->nb_streams; i++) {
		AVStream * stream = vc->inputFormatCtx->streams[i];
		AVCodecContext *codec = stream->codec;
		if (codec->codec_type == AVMEDIA_TYPE_AUDIO) {
			audioStream = i;
			break;
		}
	}
	if (audioStream < 0) {
		return -1;
	}
	printf("Found audio stream at: %d\n", audioStream);
	vc->inputAudioStream = vc->inputFormatCtx->streams[audioStream];
	vc->inputAudioCodecCtx = vc->inputAudioStream->codec;
	vc->inputAudioStreamNumber = audioStream;
	return 0;
}

int VideoConverter_findVideoCodec(VideoConverter *vc) {
	AVCodec *codec = avcodec_find_decoder(vc->inputVideoCodecCtx->codec_id);
	if (codec == NULL) {
		LOGE(1,
				"Could not find video codec for id: %d", vc->inputVideoCodecCtx->codec_id);
		VideoConverter_printAllCodecs();
		return -1;
	}
	if (avcodec_open(vc->inputVideoCodecCtx, codec) < 0) {
		LOGE(1, "Could not open video codec");
		VideoConverter_printCodecDescription(codec);
		return -2;
	}
	vc->inputVideoCodec = codec;
	return 0;
}

void VideoConverter_printCodecDescription(AVCodec *codec) {
	char *type = "???";
	switch (codec->type) {
	case AVMEDIA_TYPE_ATTACHMENT:
		type = "attachment";
		break;

	case AVMEDIA_TYPE_AUDIO:
		type = "audio";
		break;
	case AVMEDIA_TYPE_DATA:
		type = "data";
		break;
	case AVMEDIA_TYPE_NB:
		type = "nb";
		break;
	case AVMEDIA_TYPE_SUBTITLE:
		type = "subtitle";
		break;
	case AVMEDIA_TYPE_UNKNOWN:
		type = "unknown";
		break;
	case AVMEDIA_TYPE_VIDEO:
		type = "video";
		break;
	default:
		break;
	}
	LOGI(10, "id: %d codec: %s, type: %s", codec->id, codec->name, type);
}

void VideoConverter_printAllCodecs() {
	AVCodec *p = NULL;
	LOGI(10, "available codecs:");
	while ((p = av_codec_next(p)) != NULL) {
		VideoConverter_printCodecDescription(p);
	}
}

int VideoConverter_findAudioCodec(VideoConverter *vc) {
	AVCodec *codec = avcodec_find_decoder(vc->inputAudioCodecCtx->codec_id);
	if (codec == NULL) {
		LOGE(1,
				"Could not find audio codec for id: %d", vc->inputAudioCodecCtx->codec_id);
		VideoConverter_printAllCodecs();
		return -1;
	}
	if (avcodec_open(vc->inputAudioCodecCtx, codec) < 0) {
		LOGE(1, "Could not open audio codec");
		VideoConverter_printCodecDescription(codec);
		return -2;
	}
	vc->inputAudioCodec = codec;
	return 0;
}

void VideoConverter_closeAudioCodec(VideoConverter *vc) {
	avcodec_close(vc->inputAudioCodecCtx);
	vc->inputAudioCodec = NULL;
}

void VideoConverter_closeVideoCodec(VideoConverter *vc) {
	avcodec_close(vc->inputVideoCodecCtx);
	vc->inputVideoCodec = NULL;
}

void VideoConverter_createFrame(VideoConverter *vc) {
	vc->inputVideoFrame = avcodec_alloc_frame();
}

void VideoConverter_freeFrame(VideoConverter *vc) {
	av_free(vc->inputVideoFrame);
	vc->inputVideoFrame = NULL;
}

int VideoConverter_convertFrames(VideoConverter *vc) {

	unsigned int samples_size = 0;
	short *samples = NULL;
	av_write_header(vc->outputFormatCtx);
	AVPacket packet;
	while (av_read_frame(vc->inputFormatCtx, &packet) >= 0) {
		LOGI(10, "Read frame\n");
		if (packet.stream_index == vc->inputVideoStreamNumber) {

			int frameFinished;
			int ret = avcodec_decode_video2(vc->inputVideoCodecCtx,
					vc->inputVideoFrame, &frameFinished, &packet);
			if (ret < 0) {
				LOGE(1, "Fail decoding video\n");
				return -1;
			}
			if (!frameFinished) {
				//not frame yet
				LOGI(10, "Video frame not finished\n");
				continue;
			}
			printf("Decoded video frame\n");

			if (vc->outputFormatCtx->oformat->flags & AVFMT_RAWPICTURE) {
				LOGI(10, "Writing raw video frame\n");
				/* raw video case. The API will change slightly in the near
				 futur for that */
				AVPacket pkt;
				av_init_packet(&pkt);

				pkt.flags |= AV_PKT_FLAG_KEY;
				pkt.stream_index = vc->outputVideoStream->index;
				pkt.data = (uint8_t *) vc->inputVideoFrame;
				pkt.size = sizeof(AVPicture);

				ret = av_interleaved_write_frame(vc->outputFormatCtx, &pkt);
			} else {
				LOGI(10, "Encoding video frame\n");
				/* encode the image */
				int out_size = avcodec_encode_video(vc->outputVideoCodecCtx,
						vc->outputVideoBuf, vc->outputVideoBufSize,
						vc->inputVideoFrame);
				/* if zero size, it means the image was buffered */
				if (out_size > 0) {
					LOGI(10, "Writing video frame\n");
					AVPacket pkt;
					av_init_packet(&pkt);

					pkt.pts = av_rescale_q(packet.pts,
							vc->inputVideoStream->time_base,
							vc->outputVideoStream->time_base);
					if (vc->outputVideoCodecCtx->coded_frame->key_frame)
						pkt.flags |= AV_PKT_FLAG_KEY;
					pkt.stream_index = vc->outputVideoStream->index;
					pkt.data = vc->outputVideoBuf;
					pkt.size = out_size;
					pkt.dts = AV_NOPTS_VALUE;

					/* write the compressed frame in the media file */
					ret = av_interleaved_write_frame(vc->outputFormatCtx, &pkt);
					if (ret < 0) {
						LOGE(1, "Error while writing video frame: %d", ret);
						continue;
					}
					LOGI(10, "Wrote encoded video frame\n");
				} else if (out_size < 0) {
					LOGE(1, "Error while encoding video\n");
					continue;
				} else {
					LOGI(10, "Video frame buffered\n");
				}
			}
			LOGI(10, "Encoded video frame\n");

		} else if (packet.stream_index == vc->inputAudioStreamNumber) {
			if (samples_size
					< FFMAX(packet.size*sizeof(*samples), AVCODEC_MAX_AUDIO_FRAME_SIZE)) {
				samples_size =
						FFMAX(packet.size*sizeof(*samples), AVCODEC_MAX_AUDIO_FRAME_SIZE);
				av_free(samples);
				samples = av_malloc(samples_size);
				if (!samples) {
					LOGE(1, "Could not allocate sample for audio decoding");
					return -1;
				}
			}
			unsigned int decoded_data_size = samples_size;
			int ret = avcodec_decode_audio3(vc->inputAudioCodecCtx, samples,
					&decoded_data_size, &packet);
			if (ret < 0) {
				LOGE(1, "Fail decoding audio frame");
				continue;
			}
			LOGI(10, "Decoded audio frame");

			int out_size = avcodec_encode_audio(vc->outputAudioCodecCtx,
					vc->outputAudioBuf, vc->outputAudioBufSize, samples);
			if (out_size < 0) {
				LOGE(1, "Error while encoding audio");
				continue;
			}
			if (out_size > 0) {
				LOGI(10, "Writing audio frame\n");
				AVPacket pkt;
				av_init_packet(&pkt);

				pkt.pts = av_rescale_q(packet.pts,
						vc->inputAudioStream->time_base,
						vc->outputAudioStream->time_base);

				pkt.flags |= AV_PKT_FLAG_KEY;
				pkt.stream_index = vc->outputAudioStream->index;
				pkt.data = vc->outputAudioBuf;
				pkt.size = out_size;
				pkt.dts = AV_NOPTS_VALUE;

				/* write the compressed frame in the media file */
				ret = av_interleaved_write_frame(vc->outputFormatCtx, &pkt);
				if (ret < 0) {
					LOGE(1, "Error while writing audio frame\n");
					continue;
				}
				LOGI(10, "Wrote encoded audio frame\n");
			}
		}
	}
	av_write_trailer(vc->outputFormatCtx);
	return 0;
}

int VideoConverter_readFrames(VideoConverter *vc) {

	unsigned int samples_size = 0;
	short *samples = NULL;

	AVPacket packet;
	while (av_read_frame(vc->inputFormatCtx, &packet) >= 0) {
		printf("Read frame\n");
		if (packet.stream_index == vc->inputVideoStreamNumber) {

			int frameFinished;
			int ret = avcodec_decode_video2(vc->inputVideoCodecCtx,
					vc->inputVideoFrame, &frameFinished, &packet);
			if (ret < 0) {
				fprintf(stderr, "Fail decoding video\n");
				return 1;
			}
			if (!frameFinished) {
				//not frame yet
				printf("Video frame not finished\n");
				continue;
			}
			printf("Decoded video frame\n");

		} else if (packet.stream_index == vc->inputAudioStreamNumber) {
			if (samples_size
					< FFMAX(packet.size*sizeof(*samples), AVCODEC_MAX_AUDIO_FRAME_SIZE)) {
				samples_size =
						FFMAX(packet.size*sizeof(*samples), AVCODEC_MAX_AUDIO_FRAME_SIZE);
				av_free(samples);
				samples = av_malloc(samples_size);
			}
			unsigned int decoded_data_size = samples_size;
			int ret = avcodec_decode_audio3(vc->inputAudioCodecCtx, samples,
					&decoded_data_size, &packet);
			if (ret < 0) {
				fprintf(stderr, "Fail decoding audio\n");
				return 1;
			}
			printf("Decodec audio frame\n");
		}
	}
	return 0;
}

int VideoConverter_openOutputFile(VideoConverter *vc, char * fileName) {
	vc->outputFileName = fileName;
	int err = avformat_alloc_output_context2(&vc->outputFormatCtx, NULL, NULL,
			vc->outputFileName);

	if (vc->outputFormatCtx == NULL) {
		fprintf(stderr,
				"Could not create output context for file: %s, error: %d\n",
				vc->outputFileName, err);
		return -1;
	}

	vc->outputFmt = vc->outputFormatCtx->oformat;
	/* open the output file, if needed */
	if (!(vc->outputFmt->flags & AVFMT_NOFILE)) {
		if (avio_open(&vc->outputFormatCtx->pb, fileName, AVIO_FLAG_WRITE)
				< 0) { /* AVIO_FLAG_WRITE */
			fprintf(stderr, "Could not open '%s'\n", fileName);
			return -2;
		}
	}

	return 0;
}

void VideoConverter_closeOutputFile(VideoConverter *vc) {
	if (!(vc->outputFmt->flags & AVFMT_NOFILE)) {
		/* close the output file */
		avio_close(vc->outputFormatCtx->pb);
	}

	vc->outputFileName = NULL;
//	av_close_input_file(vc->outputFormatCtx);
//	avformat_free_context(vc->outputFormatCtx);
	av_free(vc->outputFormatCtx);
	vc->outputFormatCtx = NULL;
	vc->outputFmt = NULL;
}

int VideoConverter_hasOutputVideoCodec(VideoConverter *vc) {
	return vc->outputFmt->video_codec != CODEC_ID_NONE;
}

int VideoConverter_hasOutputAudioCodec(VideoConverter *vc) {
	return vc->outputFmt->audio_codec != CODEC_ID_NONE;
}

int VideoConverter_createVideoStream(VideoConverter *vc) {
	//
	//AVStream *st;

	vc->outputVideoStream = av_new_stream(vc->outputFormatCtx, 0);
	if (!vc->outputVideoStream) {
		fprintf(stderr, "Could not alloc stream\n");
		return -1;
	}
	LOGI(10, "Selected codec for video output stream");
	AVCodecContext *c = vc->outputVideoStream->codec;
	AVCodecContext *iC = vc->inputVideoCodecCtx;
	vc->outputVideoCodecCtx = c;

	c->codec_id = vc->outputFmt->video_codec;
	c->codec_type = AVMEDIA_TYPE_VIDEO;

	/* put sample parameters */
	c->bit_rate = iC->bit_rate; //400000
	/* resolution must be a multiple of two */
	c->width = iC->width; // 352
	c->height = iC->height; // 288
	/* time base: this is the fundamental unit of time (in seconds) in terms
	 of which frame timestamps are represented. for fixed-fps content,
	 timebase should be 1/framerate and timestamp increments should be
	 identically 1. */
	c->time_base.den = 25; // STREAM_FRAME_RATE
	c->time_base.num = 1; // 1
	c->gop_size = 12; /* emit one intra frame every twelve frames at most */
	c->pix_fmt = STREAM_PIX_FMT;
	if (c->codec_id == CODEC_ID_MPEG2VIDEO) {
		/* just for testing, we also add B frames */
		c->max_b_frames = 2;
	}
	if (c->codec_id == CODEC_ID_MPEG1VIDEO) {
		/* Needed to avoid using macroblocks in which some coeffs overflow.
		 This does not happen with normal video, it just happens here as
		 the motion of the chroma plane does not match the luma plane. */
		c->mb_decision = 1;
	}
	// some formats want stream headers to be separate
	if (vc->outputFormatCtx->oformat->flags & AVFMT_GLOBALHEADER)
		c->flags |= CODEC_FLAG_GLOBAL_HEADER;

	return 0;
}

int VideoConverter_openVideoStream(VideoConverter *vc) {
	AVCodecContext *c = vc->outputVideoCodecCtx;

	vc->outputVideoCodecCtx = vc->outputVideoStream->codec;

	/* find the video encoder */
	vc->outputVideoCodec = avcodec_find_encoder(
			vc->outputVideoCodecCtx->codec_id);
	if (!vc->outputVideoCodec) {
		LOGE(1, "Video codec not found");
		VideoConverter_printAllCodecs();
		return -1;
	}
	LOGI(10, "Found video output codec:");
	VideoConverter_printCodecDescription(vc->outputVideoCodec);
	/* open the codec */
	if (avcodec_open(vc->outputVideoCodecCtx, vc->outputVideoCodec) < 0) {
		LOGE(1, "Could not open video codec\n");
		return -2;
	}

	vc->outputVideoBuf = NULL;
	vc->outputVideoBufSize = 0;
	if (!(vc->outputFormatCtx->oformat->flags & AVFMT_RAWPICTURE)) {
		/* allocate output buffer */
		/* XXX: API change will be done */
		/* buffers passed into lav* can be allocated any way you prefer,
		 as long as they're aligned enough for the architecture, and
		 they're freed appropriately (such as using av_free for buffers
		 allocated with av_malloc) */
		vc->outputVideoBufSize = 200000;
		vc->outputVideoBuf = av_malloc(vc->outputVideoBufSize);
	} else {
		printf("no AVFMT_RAWPICTURE\n");
	}
	return 0;
}

void VideoConverter_closeVideoStream(VideoConverter *vc) {
	avcodec_close(vc->outputVideoStream->codec);
	av_free(vc->outputVideoBuf);
}

void VideoConverter_freeVideoStream(VideoConverter *vc) {
	av_free(vc->outputVideoStream);
}

int VideoConverter_createAudioStream(VideoConverter *vc) {
	vc->outputAudioStream = av_new_stream(vc->outputFormatCtx, 1);
	if (!vc->outputAudioStream) {
		fprintf(stderr, "Could not alloc audio stream\n");
		return -1;
	}

	AVCodecContext *c = vc->outputAudioStream->codec;
	AVCodecContext *iC = vc->inputAudioCodecCtx;
	vc->outputAudioCodecCtx = c;
	c->codec_id = vc->outputFmt->audio_codec;
	c->codec_type = AVMEDIA_TYPE_AUDIO;

	/* put sample parameters */
	c->sample_fmt = AV_SAMPLE_FMT_S16;
	c->bit_rate = 64000;
	//c->sample_rate = 44100;
	//c->sample_fmt = iC->sample_fmt;
	//c->bit_rate = iC->bit_rate;
	c->sample_rate = iC->sample_rate;
	c->channels = iC->channels;

	// some formats want stream headers to be separate
	if (vc->outputFormatCtx->oformat->flags & AVFMT_GLOBALHEADER)
		c->flags |= CODEC_FLAG_GLOBAL_HEADER;

	return 0;
}

int VideoConverter_openAudioStream(VideoConverter *vc) {
	vc->outputAudioCodec = avcodec_find_encoder(
			vc->outputAudioCodecCtx->codec_id);
	if (!vc->outputAudioCodec) {
		LOGE(1, "audio output codec not found");
		VideoConverter_printAllCodecs();
		return -1;
	}
	LOGI(10, "found audio output codec:");
	VideoConverter_printCodecDescription(vc->outputAudioCodec);

	/* open it */
	if (avcodec_open(vc->outputAudioCodecCtx, vc->outputAudioCodec) < 0) {
		LOGE(1, "could not open audio codec\n");
		return -2;
	}

	vc->outputAudioBufSize = 10000;
	vc->outputAudioBuf = av_malloc(vc->outputAudioBufSize);
	return 0;
}

void VideoConverter_closeAudioStream(VideoConverter *vc) {
	avcodec_close(vc->outputAudioStream->codec);
	av_free(vc->outputAudioBuf);
}

void VideoConverter_freeAudioStream(VideoConverter *vc) {
	av_free(vc->outputAudioStream);
}
