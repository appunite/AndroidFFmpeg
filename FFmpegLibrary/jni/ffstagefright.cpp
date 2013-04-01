/*
 * ffstagefright.cpp
 * Copyright (c) 2012 Jacek Marchwicki from Appunite.com
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
 * Speciall thanks to Mohamed Naufal and Martin Storsjö for giving
 * an example of using stagefright
 *
 */
#include <binder/ProcessState.h>
#include <media/stagefright/MetaData.h>
#include <media/stagefright/MediaBufferGroup.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/OMXClient.h>
#include <media/stagefright/OMXCodec.h>
#include <utils/List.h>
#include <new>
#include <map>
#include <libyuv.h>
#include <iostream>
#include<stdio.h>

extern "C" {
#include "sync.h"
#include <dlfcn.h>
#include "ffstagefright.h"
#include "libavutil/internal.h"
#include "libavcodec/avcodec.h"
#include "libavutil/imgutils.h"
#include "libavutil/mem.h"
}

using namespace android;

class FFMediaSource;

#define OUT_FRAME_BUFFER_SIZE 4

typedef void (*OMXClientConstructor)(OMXClient *);

struct FFStagefrightContext {
    AVCodecContext *avctx;
    AVBitStreamFilterContext *bsfc;

    sp<ANativeWindow> mNativeWindow;

    WaitFunc *mWaitFunc;
    void *mWaitFuncData;
    int mStreamNo;

    void* mHandle;

    pthread_mutex_t mMutex;
    pthread_cond_t mCond;
    bool mInThreadStop;
    uint8_t *mInData;
    int mInSize;
    int64_t mInPts;
    int mInFlag;

    bool mNextFrameIsFlushEnd;
    bool mOutThreadFlush;

    pthread_t mDecodeThreadId;
    bool mOutThreadStarted;
    bool mOutThreadExited;
    int mOutDecodeError;
    int mOutColorFormat;
    AVFrame mOutFrameBuffer[OUT_FRAME_BUFFER_SIZE];
    int mOutFrameCurrent;
    int mOutFrameLast;
    int mDisplayWidth;
    int mDisplayHeight;

    OMXClient *mClient;
    sp<MediaSource> mDecoder;
    sp<MediaSource> mSource;
};

static inline int get_next_out_frame(int i) {
	return (i + 1) % OUT_FRAME_BUFFER_SIZE;
}

inline int is_buffer_empty(FFStagefrightContext *ctx) {
	return ctx->mOutFrameCurrent == ctx->mOutFrameLast;
}

inline int is_buffer_full(FFStagefrightContext * ctx) {
	return get_next_out_frame(ctx->mOutFrameLast) == ctx->mOutFrameCurrent;
}

class FFMediaSource : public MediaSource {
public:
    FFMediaSource(AVCodecContext *avctx, sp<MetaData> meta) : MediaSource() {
        s = (FFStagefrightContext*)avctx->priv_data;
        source_meta = meta;
        frame_size = avctx->width * avctx->height * 4;
        buf_group.add_buffer(new MediaBuffer(frame_size));
    }

    virtual sp<MetaData> getFormat() {
        return source_meta;
    }

    virtual status_t start(MetaData *params = NULL) {
        return OK;
    }

    virtual status_t stop() {
        return OK;
    }

    virtual status_t read(
                MediaBuffer **buffer, const ReadOptions *options = NULL) {

        status_t ret;


		av_log(s->avctx, AV_LOG_DEBUG, "read - waiting for lock\n");
        pthread_mutex_lock(&s->mMutex);
        for (;;) {
//        	if (s->mInThreadFlush) {
//        		av_log(s->avctx, AV_LOG_DEBUG, "read - flush\n");
//        		// We using this INFO that data should be flushed
//        		// to this frame
//        		s->mInThreadFlush = false;
//        		pthread_cond_broadcast(&s->mCond);
//
//        		ret = buf_group.acquire_buffer(buffer);
//				if (ret == OK) {av_log(
//					s->avctx, AV_LOG_DEBUG, "read - acquired buffer\n");
//					(*buffer)->set_range(0, 0);
//					(*buffer)->meta_data()->clear();
//					(*buffer)->meta_data()->setInt32(kKeyAlbum, 1);
//					(*buffer)->meta_data()->setInt64(kKeyTime, s->mInPts);
//				} else {
//					av_log(s->avctx, AV_LOG_ERROR, "Failed to acquire MediaBuffer\n");
//				}
//        		av_log(s->avctx, AV_LOG_DEBUG, "read - sending flush packet\n");
//        		goto end;
//        	}
        	if (s->mInThreadStop) {
        		av_log(s->avctx, AV_LOG_DEBUG, "read - exiting\n");
        		s->mInThreadStop = false;
        		ret = ERROR_END_OF_STREAM;
        		goto end;
        	}
        	if (s->mInData != NULL) {
        		av_log(s->avctx, AV_LOG_DEBUG, "read - found packet\n");
        		ret = buf_group.acquire_buffer(buffer);
				if (ret == OK) {
					av_log(s->avctx, AV_LOG_DEBUG, "read - acquired buffer\n");
					(*buffer)->set_range(0, s->mInSize);
					memcpy((uint8_t *)(*buffer)->data() + (*buffer)->range_offset(), s->mInData, s->mInSize);
					(*buffer)->meta_data()->clear();
					(*buffer)->meta_data()->setInt32(kKeyIsSyncFrame, s->mInFlag & AV_PKT_FLAG_KEY ? 1 : 0);
					(*buffer)->meta_data()->setInt64(kKeyTime, s->mInPts);
//					int32_t isFlushEnd = 0;
//					if (s->mNextFrameIsFlushEnd) {
//		        		av_log(s->avctx, AV_LOG_DEBUG, "read - setting to flush end\n");
//						isFlushEnd = 1;
//						s->mNextFrameIsFlushEnd = false;
//					}
//					(*buffer)->meta_data()->setInt32(kKeyAlbum, isFlushEnd);
				} else {
					av_log(s->avctx, AV_LOG_ERROR, "Failed to acquire MediaBuffer\n");
				}
				av_free(s->mInData);
				s->mInData = NULL;
				pthread_cond_broadcast(&s->mCond);
				ret = OK;
				goto end;
        	}

    		av_log(s->avctx, AV_LOG_DEBUG, "read - cond wait\n");
        	pthread_cond_wait(&s->mCond, &s->mMutex);
    		av_log(s->avctx, AV_LOG_DEBUG, "read - cond wait (exit)\n");
        }
end:
        pthread_mutex_unlock(&s->mMutex);
		av_log(s->avctx, AV_LOG_DEBUG, "read - unlocked\n");
        return ret;
    }
protected:
    virtual ~FFMediaSource(){};
private:
    MediaBufferGroup buf_group;
    sp<MetaData> source_meta;
    FFStagefrightContext *s;
    int frame_size;
};

void* decode_thread(void *arg)
{
    AVCodecContext *avctx = (AVCodecContext*)arg;
    FFStagefrightContext *s = (FFStagefrightContext*)avctx->priv_data;

    MediaBuffer *buffer;
    status_t status;

    int src_linesize[4];
    uint8_t *src_data[4];
    unsigned char *ptr;

    int64_t timeUs;
    int32_t stop_flush;

    bool loop = true;

	while (loop) {
		av_log(s->avctx, AV_LOG_DEBUG, "thread - waiting for frame\n");
		status = s->mDecoder->read(&buffer);
		av_log(s->avctx, AV_LOG_DEBUG, "thread - read frame\n");
		if (status == OK) {
			av_log(s->avctx, AV_LOG_DEBUG, "thread - status\n");

			sp < MetaData > metaData = buffer->meta_data();
			// FIXME todo
//			metaData->findInt32(kKeyAlbum, &stop_flush);
//			if (stop_flush) {
//				// FIXME does not work because kKeyAlbum is not passed from reader
//				s->mOutThreadFlush = false;
//				av_log(s->avctx, AV_LOG_DEBUG, "thread - stop skipping\n");
//			}

			if (s->mOutThreadFlush) {
				av_log(s->avctx, AV_LOG_DEBUG, "thread - skipping frame because flush\n");
			} else if (buffer->range_length() > 0) {
				av_log(s->avctx, AV_LOG_DEBUG, "thread - length1\n");

				ANativeWindow * window = s->mNativeWindow.get();
				metaData->findInt64(kKeyTime, &timeUs);

				av_log(s->avctx, AV_LOG_DEBUG, "thread - length2: %f\n", timeUs / 1000.0);

				enum WaitFuncRet ret = s->mWaitFunc(s->mWaitFuncData,
						timeUs / 1000.0, s->mStreamNo);
				av_log(s->avctx, AV_LOG_DEBUG, "thread - length3 - ret %d\n", ret);
				if (ret == WAIT_FUNC_RET_OK) {
//					sp <GraphicBuffer > gBuffer = buffer->graphicBuffer();
//					GraphicBuffer * gb = gBuffer.get();
//					ANativeWindowBuffer *nb = gb;

					av_log(s->avctx, AV_LOG_DEBUG, "thread - length4\n");
					native_window_set_buffers_timestamp(window, timeUs * 1000);
#if SOURCE_VERSION_CODE >= 16
					status_t err = window->queueBuffer_DEPRECATED(window, buffer->graphicBuffer().get());
#else
					status_t err = window->queueBuffer(window, nb);
#endif

					if (err == 0) {
						sp < MetaData > metaData = buffer->meta_data();
						metaData->setInt32(kKeyRendered, 1);
					} else {
						av_log(s->avctx, AV_LOG_ERROR,
								"queueBuffer failed with error %s (%d)", strerror(-err), -err);
						loop = false;
					}
				} else if (ret == WAIT_FUNC_RET_SKIP) {
					av_log(s->avctx, AV_LOG_DEBUG, "thread - skipping frame because retfunc\n");
				} else {
					av_log(s->avctx, AV_LOG_PANIC, "thread - unknown return type\n");
					loop = false;
				}

			}
		} else if (status == INFO_FORMAT_CHANGED) {
			av_log(s->avctx, AV_LOG_DEBUG,
					"thread - received INFO_FORMAT_CHANGED\n");
		} else if (status == TIMED_OUT) {
			av_log(s->avctx, AV_LOG_WARNING, "thread - TIMED_OUT\n");
		} else if (status == ERROR_END_OF_STREAM) {
			av_log(s->avctx, AV_LOG_DEBUG, "thread - end of stream\n");
			loop = false;
		} else {
			av_log(avctx, AV_LOG_ERROR, "error decoding: %x\n", status);
			pthread_mutex_lock(&s->mMutex);
			s->mOutDecodeError = true;
			pthread_mutex_unlock(&s->mMutex);
			loop = false;
		}

		if (buffer) {
			av_log(avctx, AV_LOG_DEBUG, "thread - releasing\n");
			buffer->release();
		}
	}

	av_log(avctx, AV_LOG_DEBUG, "thread - exiting thread\n");
    pthread_mutex_lock(&s->mMutex);
	s->mOutThreadExited = true;
	pthread_cond_broadcast(&s->mCond);
	pthread_mutex_unlock(&s->mMutex);

	av_log(avctx, AV_LOG_DEBUG, "thread - closed thread\n");
    return 0;
}


static av_cold int ff_stagefright_close(AVCodecContext *avctx)
{
    FFStagefrightContext *s = (FFStagefrightContext*)avctx->priv_data;
    int ret = 0;
    int err = 0;

	av_log(avctx, AV_LOG_DEBUG, "requested OMX codec close\n");

	if (s->mSource != NULL) {
		pthread_mutex_lock(&s->mMutex);
		s->mInThreadStop = true;
		pthread_cond_broadcast(&s->mCond);
		pthread_mutex_unlock(&s->mMutex);
	}


	av_log(avctx, AV_LOG_DEBUG, "Close: 1\n");

    if (s->mOutThreadStarted) {
    	pthread_mutex_lock(&s->mMutex);
    	while (!s->mOutThreadExited) {
    		pthread_cond_wait(&s->mCond, &s->mMutex);
    	}
    	ret = pthread_join(s->mDecodeThreadId, NULL);
    	if (err == 0 && ret != 0) {
    		err = ret;
    	}
    	pthread_mutex_unlock(&s->mMutex);

    	s->mOutThreadStarted = false;
    }


	av_log(avctx, AV_LOG_DEBUG, "Close: 2\n");

    if (s->mDecoder != NULL) {
    	s->mDecoder->stop();
    	s->mDecoder = NULL;
    }


	av_log(avctx, AV_LOG_DEBUG, "Close: 3\n");

    if (s->mSource != NULL) {
    	s->mSource = NULL;
    }


	av_log(avctx, AV_LOG_DEBUG, "Close: 4\n");

    if (s->bsfc) {
        av_bitstream_filter_close(s->bsfc);
        s->bsfc = NULL;
    }


	av_log(avctx, AV_LOG_DEBUG, "Close: 5\n");

    if (s->mClient != NULL) {
    	s->mClient->disconnect();
        av_free(s->mClient);
//    	delete s->mClient;
    	s->mClient = NULL;
    }


	av_log(avctx, AV_LOG_DEBUG, "Close: 6\n");

    if (s->mHandle != NULL) {
    	dlclose(s->mHandle);
    	s->mHandle = NULL;
    }


	av_log(avctx, AV_LOG_DEBUG, "Close: 7\n");

    for (int i = 0; i < OUT_FRAME_BUFFER_SIZE; ++i) {
		AVFrame * frame = &s->mOutFrameBuffer[i];
		avctx->release_buffer(avctx, frame);
	}


	av_log(avctx, AV_LOG_DEBUG, "Close: 8\n");

    memset(s->mOutFrameBuffer, 0, OUT_FRAME_BUFFER_SIZE * sizeof(s->mOutFrameBuffer[0]));


	av_log(avctx, AV_LOG_DEBUG, "Close: 9\n");

    memset(s, 0, sizeof(s));



	av_log(avctx, AV_LOG_DEBUG, "closed OMX codec\n");

    return err;
}

static av_cold int ff_stagefright_init(AVCodecContext *avctx)
{
    FFStagefrightContext *s = (FFStagefrightContext*)avctx->priv_data;
    sp<MetaData> meta, outFormat;
    int32_t colorFormat = 0;
    int ret = 0;
    OMXClientConstructor omxConstructor;
    const char *error;
    uint32_t flags = 0;

    StageFrightData *data = reinterpret_cast<StageFrightData*>(avctx->opaque);


    memset(s, 0, sizeof(s));

    s->mNativeWindow = data->window;
    s->mWaitFunc = data->wait_func;
    s->mWaitFuncData = data->data;
    s->mStreamNo = data->stream_no;

    s->mOutFrameCurrent = 0;
    s->mOutFrameLast = 0;

    for (int i = 0; i < OUT_FRAME_BUFFER_SIZE; ++i) {
    	AVFrame * frame = &s->mOutFrameBuffer[i];
		avcodec_get_frame_defaults(frame);
		frame->reference = 3;
	}

    av_log(avctx, AV_LOG_DEBUG, "initial stagefright");

    if (!avctx->extradata || !avctx->extradata_size || avctx->extradata[0] != 1) {
    	ret = -1;
        av_log(avctx, AV_LOG_ERROR, "Cannot read h264 codec extra data!\n");
    	goto fail;
    }

    av_log(avctx, AV_LOG_DEBUG, "2a");

    s->avctx = avctx;
    s->bsfc  = av_bitstream_filter_init("h264_mp4toannexb");
    if (!s->bsfc) {
        av_log(avctx, AV_LOG_ERROR, "Cannot open the h264_mp4toannexb BSF!\n");
        ret = -1;
        goto fail;
    }


    av_log(avctx, AV_LOG_DEBUG, "2b");

	s->mHandle = dlopen("libstagefright.so", RTLD_NOW);
	error = dlerror();
	if (s->mHandle == NULL) {
		av_log(avctx, AV_LOG_ERROR, "Cannot open libstagefrith.so: %s", error);
		ret = -1;
		goto fail;
	}


    av_log(avctx, AV_LOG_DEBUG, "2c");
    av_log(avctx, AV_LOG_DEBUG, "d");

    *reinterpret_cast<void**>(&omxConstructor) =
    		dlsym(s->mHandle, "_ZN7android9OMXClientC1Ev");
	error = dlerror();
	if (omxConstructor == NULL || error != NULL) {
		if (error != NULL) {
			av_log(avctx, AV_LOG_ERROR,
					"Cannot find OMX:Client::OMXClient() method: %s",
					error);
		} else {
			av_log(avctx, AV_LOG_ERROR,
					"Unknown error while loading OMX:Client::OMXClient() method");
		}
		ret = -1;
		goto fail;
	}

    av_log(avctx, AV_LOG_DEBUG, "2d");

    /**
     * new OMXClient
     *
     * arm-linux-androideabi-readelf -Ws libstagefright.so | grep OMXClient
     * arm-linux-androideabi-c++filt _ZN7android9OMXClientC2Ev
     * gives > android::OMXClient::OMXClient()
     */
    s->mClient = new OMXClient;
//    s->mClient = static_cast<OMXClient *>(av_malloc(sizeof(OMXClient *)));
//    omxConstructor(s->mClient);
//    s->mClient = new OMXClient();
    if (s->mClient == NULL) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    av_log(avctx, AV_LOG_DEBUG, "2e");

    if (s->mClient->connect() !=  OK) {
        av_log(avctx, AV_LOG_ERROR, "Cannot connect OMX client\n");
//        delete s->mClient;
        ret = -1;
        goto fail;
    }


    av_log(avctx, AV_LOG_DEBUG, "3");
    s->mInThreadStop = false;
    s->mNextFrameIsFlushEnd = false;
    s->mOutThreadFlush = false;
    meta = new MetaData;
    if (meta == NULL) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    meta->setCString(kKeyMIMEType, MEDIA_MIMETYPE_VIDEO_AVC);
    meta->setInt32(kKeyBitRate, avctx->bit_rate);
    meta->setInt32(kKeyWidth, avctx->width);
    meta->setInt32(kKeyHeight, avctx->height);
    meta->setData(kKeyAVCC, kTypeAVCC, avctx->extradata, avctx->extradata_size);


    av_log(avctx, AV_LOG_DEBUG, "4");
    android::ProcessState::self()->startThreadPool();

    av_log(avctx, AV_LOG_DEBUG, "5");
    s->mSource = new FFMediaSource(avctx, meta);
    if (s->mSource == NULL) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

//    flags |= OMXCodec::kSoftwareCodecsOnly;
    flags |= OMXCodec::kHardwareCodecsOnly;
//    flags |= OMXCodec::kClientNeedsFramebuffer;
//    flags |= OMXCodec::kEnableGrallocUsageProtected;
    if (s->mNativeWindow == NULL) {
    	av_log(avctx, AV_LOG_ERROR, "stagefright - window could not be null\n");
    	ret = -1;
    	goto fail;
    }
    av_log(avctx, AV_LOG_DEBUG, "6");
    s->mDecoder = OMXCodec::Create(s->mClient->interface(), meta,
                                  false, s->mSource, NULL, flags,
                                  s->mNativeWindow
    	);
    av_log(avctx, AV_LOG_DEBUG, "6a");
    if (s->mDecoder == NULL) {
    	av_log(avctx, AV_LOG_ERROR, "stagefright - Cannot find decoder\n");
		ret = -1;
    	goto fail;
    }
    if (s->mDecoder->start() !=  OK) {
        av_log(avctx, AV_LOG_ERROR, "stagefright - Cannot start decoder\n");
        ret = -1;
        goto fail;
    }


    av_log(avctx, AV_LOG_DEBUG, "7");

    outFormat = s->mDecoder->getFormat();
    outFormat->findInt32(kKeyColorFormat, &colorFormat);
    s->mOutColorFormat = colorFormat;
    if (colorFormat == OMX_QCOM_COLOR_FormatYVU420SemiPlanar ||
        colorFormat == OMX_COLOR_FormatYUV420SemiPlanar) {
    	av_log(avctx, AV_LOG_DEBUG, "Color: PIX_FMT_NV21");
        avctx->pix_fmt = PIX_FMT_NV21;
    } else if (colorFormat == OMX_COLOR_FormatYCbYCr) {
    	av_log(avctx, AV_LOG_DEBUG, "Color: PIX_FMT_YUYV422");
        avctx->pix_fmt = PIX_FMT_YUYV422;
    } else if (colorFormat == OMX_COLOR_FormatCbYCrY) {
    	av_log(avctx, AV_LOG_DEBUG, "Color: PIX_FMT_UYVY422");
        avctx->pix_fmt = PIX_FMT_UYVY422;
    } else if (colorFormat == OMX_TI_COLOR_FormatYUV420PackedSemiPlanar) {
    	av_log(avctx, AV_LOG_DEBUG, "Color: PIX_FMT_NV12: %d", colorFormat);
        avctx->pix_fmt = PIX_FMT_NV12;
    } else if (colorFormat == OMX_COLOR_FormatYUV420Planar){
    	av_log(avctx, AV_LOG_DEBUG, "Color: PIX_FMT_YUV420P: %d", colorFormat);
        avctx->pix_fmt = PIX_FMT_YUV420P;
    } else if (colorFormat == OMX_COLOR_Format16bitRGB565) {
    	av_log(avctx, AV_LOG_DEBUG, "Color: PIX_FMT_RGB565");
    	avctx->pix_fmt = PIX_FMT_RGB565;
    } else {
    	av_log(avctx, AV_LOG_DEBUG, "Color: unknown: %d", colorFormat);
        avctx->pix_fmt = PIX_FMT_YUV420P;
    }
    outFormat = NULL;

    pthread_mutex_init(&s->mMutex, NULL);
    pthread_cond_init(&s->mCond, NULL);

    s->mOutThreadExited = false;

	av_log(avctx, AV_LOG_DEBUG, "started thread\n");
	if ((ret = pthread_create(&s->mDecodeThreadId, NULL, &decode_thread, avctx)) != 0) {
		goto fail;
	}
	s->mOutThreadStarted = true;

	av_log(avctx, AV_LOG_DEBUG, "8");
    return ret;

fail:
	av_log(avctx, AV_LOG_ERROR, "Error while initializing OMX decoder");
    ff_stagefright_close(avctx);
    return ret;
}

static int ff_stagefright_decode_frame(AVCodecContext *avctx, void *data,
                                    int *data_size, AVPacket *avpkt)
{
    FFStagefrightContext *s = (FFStagefrightContext*)avctx->priv_data;
    int buf_size       = avpkt->size;
    AVFrame *pict      = (AVFrame*)data;
    int ret            = 0;
    double time;
	av_log(avctx, AV_LOG_DEBUG, "started decoding frame\n");

    av_log(s->avctx, AV_LOG_DEBUG, "decode_frame - waiting for decoded frame\n");
	pthread_mutex_lock(&s->mMutex);
	if (s->mOutDecodeError) {
		av_log(avctx, AV_LOG_ERROR, "Error in decoder\n");
		ret = -1;
	} else if (!is_buffer_empty(s)) {
		av_log(s->avctx, AV_LOG_DEBUG, "decode_frame - there is a decoded frame\n");
		int current = s->mOutFrameCurrent;

		*pict = s->mOutFrameBuffer[current];
		*data_size = sizeof(AVFrame);

		s->mOutFrameCurrent = get_next_out_frame(current);
		pthread_cond_broadcast(&s->mCond);
	} else {
		av_log(s->avctx, AV_LOG_DEBUG, "decode_frame - there is no decoded frame\n");
		*data_size = 0;
	}
	pthread_mutex_unlock(&s->mMutex);


    if (buf_size != 0 && ret == 0) {
		av_log(s->avctx, AV_LOG_DEBUG, "decode_frame - writing frame for decode\n");
		pthread_mutex_lock(&s->mMutex);

		// From now is better for you to do not use avpkt->data and avpkt->size because
		// this values can be changed by av_bitstream_filter_filter
		ret = av_bitstream_filter_filter(s->bsfc, avctx, NULL, &s->mInData, &s->mInSize,
				avpkt->data, avpkt->size, avpkt->flags & AV_PKT_FLAG_KEY);
		if (ret < 0) {
			av_log(s->avctx, AV_LOG_ERROR, "Error while using bitstream filter\n");
			goto error;
		}

		s->mInPts = avpkt->pts;
		if (s->mInPts == AV_NOPTS_VALUE) {
			s->mInPts = 0;
		}


		time = (double) s->mInPts * av_q2d(avctx->time_base);
		av_log(s->avctx, AV_LOG_DEBUG, "decode_frame - pts: %f", time);


		// rescale to miliseconds
		s->mInPts = av_rescale_q(s->mInPts, avctx->time_base, (AVRational){1, 1000});

		s->mInFlag = avpkt->flags;
		pthread_cond_broadcast(&s->mCond);

		av_log(s->avctx, AV_LOG_DEBUG, "decode_frame - waiting for write\n");
		while (s->mInData != NULL)
			pthread_cond_wait(&s->mCond, &s->mMutex);

		av_log(s->avctx, AV_LOG_DEBUG, "decode_frame - wrote frame\n");
		ret = buf_size;
error:
		pthread_mutex_unlock(&s->mMutex);
    }

	av_log(avctx, AV_LOG_DEBUG, "exiting decoding frame\n");
    return ret;
}

void av_cold ff_stagefright_flush(AVCodecContext * avctx) {
	FFStagefrightContext *s = (FFStagefrightContext*)avctx->priv_data;
	av_log(avctx, AV_LOG_DEBUG, "ff_stagefright_flush flushing\n");

	pthread_mutex_lock(&s->mMutex);
	av_log(avctx, AV_LOG_DEBUG, "ff_stagefright_flush stopping threads\n");
	s->mNextFrameIsFlushEnd = true;
	s->mOutThreadFlush = true;
	pthread_cond_broadcast(&s->mCond);
	av_log(avctx, AV_LOG_DEBUG, "ff_stagefright_flush finishing\n");
	pthread_mutex_unlock(&s->mMutex);
}

AVCodec ff_libstagefright_h264_decoder = {
    "stagefright_h264",								// name
    NULL_IF_CONFIG_SMALL("stagefright H.264"),		// long_name
    AVMEDIA_TYPE_VIDEO,								// type
    AV_CODEC_ID_H264,								// id
    CODEC_CAP_DELAY,								// capabilities
    NULL,											// supported_framerates
    NULL,											// pix_fmts
    NULL,											// supported_samplerates
    NULL,											// sample_fmts
    NULL,											// channel_layouts
    0,												// max_lowres
    NULL,											// priv_class
    NULL,											// profiles
    sizeof(FFStagefrightContext),					// priv_data_size
    NULL,											// next
    NULL,											// init_thread_copy
    NULL,											// update_thread_context
    NULL,											// defaults
    NULL,											// init_static_data
    ff_stagefright_init,							// init
    NULL,											// encode
    NULL,											// encode2
    ff_stagefright_decode_frame,					// decode
    ff_stagefright_close,							// close
    ff_stagefright_flush							// flush
};

void register_stagefright_codec() {
	avcodec_register(&ff_libstagefright_h264_decoder);
}
