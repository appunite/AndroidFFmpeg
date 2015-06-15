/*
 * aes-protocol.c
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
#include <libavutil/opt.h>

#include "ffmpeg/libavformat/url.h"

#include <tropicssl/base64.h>
#include <tropicssl/havege.h>
#include <tropicssl/sha2.h>
#include <tropicssl/aes.h>

#include "aes-protocol.h"

#define FALSE (0)
#define TRUE (!FALSE)

#include <android/log.h>
#define LOG_LEVEL 2
#define LOG_TAG "aes-protocol.c"
#define LOGI(level, ...) if (level <= LOG_LEVEL) {__android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__);}
#define LOGE(level, ...) if (level <= LOG_LEVEL) {__android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__);}

#define RAW_KEY_SIZE	    24
#define BASE64_KEY_SIZE	    ((4 * RAW_KEY_SIZE) / 3)
#define SHA256_KEY_SIZE		32
#define AES_KEY_SIZE        16
#define BUFFER_SIZE			512

typedef struct {
	const AVClass *class;
	URLContext *hd;
	uint8_t *key;
	aes_context aes;
	unsigned char iv[AES_KEY_SIZE];
	unsigned char read_buff[BUFFER_SIZE];
	unsigned char decoded_buff[BUFFER_SIZE];
	int64_t reading_position;
	int64_t read_start_point;
	int64_t read_end_point;
	int64_t stream_end;
} AesContext;

#define OFFSET(x) offsetof(AesContext, x)

static const AVOption options[] = { { "aeskey", "AES decryption key",
		OFFSET(key), AV_OPT_TYPE_STRING, .flags = AV_OPT_FLAG_DECODING_PARAM },
		{ NULL } };

static const AVClass aes_class = { .class_name = "aes", .item_name =
		av_default_item_name, .option = options, .version =
		LIBAVUTIL_VERSION_INT, };

#define MAX_PRINT_LEN 2048

#if LOG_LEVEL >= 10
static char print_buff[MAX_PRINT_LEN * 2 + 1];
#endif

static void log_hex(char *log, char *data, int len) {
#if LOG_LEVEL >= 10
	int i;
	if (len > MAX_PRINT_LEN) {
		LOGI(1,
				"log_hex: oversized log requested: %d, max size: %d", len, MAX_PRINT_LEN);
		len = MAX_PRINT_LEN;
	}
	for (i = 0; i < len; ++i)
		sprintf(&print_buff[i * 2], "%02X", (unsigned char) data[i]);
	LOGI(10, log, len, print_buff);
#endif
}

static int aes_open(URLContext *h, const char *uri, int flags) {
	const char *nested_url;
	int ret = 0;
	AesContext *c = h->priv_data;
	LOGI(3, "aes_open: opening data");

	if (!av_strstart(uri, "aes+", &nested_url)
			&& !av_strstart(uri, "aes:", &nested_url)) {
		av_log(h, AV_LOG_ERROR, "Unsupported url %s", uri);
		LOGE(1, "Unsupported url %s", uri);
		ret = AVERROR(EINVAL);
		goto err;
	}

	if (c->key == NULL) {
		av_log(h, AV_LOG_ERROR, "Key is not set\n");
		LOGE(1, "Key is not set");
		ret = AVERROR(EINVAL);
		goto err;
	}
	if (strlen(c->key) != BASE64_KEY_SIZE) {
		av_log(h, AV_LOG_ERROR, "Wrong size of key\n");
		LOGE(1, "Wrong size of key");
		ret = AVERROR(EINVAL);
		goto err;
	}
	if (flags & AVIO_FLAG_WRITE) {
		av_log(h, AV_LOG_ERROR, "Only decryption is supported currently\n");
		LOGE(1, "Only decryption is supported currently");
		ret = AVERROR(ENOSYS);
		goto err;
	}
	if ((ret = ffurl_open(&c->hd, nested_url, AVIO_FLAG_READ,
			&h->interrupt_callback, NULL)) < 0) {
		av_log(h, AV_LOG_ERROR, "Unable to open input\n");
		LOGE(1, "Unable to open input");
		goto err;
	}
	LOGI(3, "aes_open: opened data with key: %s", c->key);
	log_hex("aes_open: raw_key[%d]: %s", c->key, RAW_KEY_SIZE);

	memset(c->iv, 0, AES_KEY_SIZE);
	memset(c->read_buff, 0, BUFFER_SIZE);
	memset(c->decoded_buff, 0, BUFFER_SIZE);
	c->reading_position = 0;
	c->read_start_point = 0;
	c->read_end_point = 0;
	c->stream_end = -1;

	unsigned char sha256_key[SHA256_KEY_SIZE];
	sha2_context ctx;
	sha2_starts(&ctx, 0);
	sha2_update(&ctx, c->key, BASE64_KEY_SIZE);
	sha2_finish(&ctx, sha256_key);
	log_hex("aes_open: sha256_key[%d]: %s", sha256_key, SHA256_KEY_SIZE);

	unsigned char aes_key[AES_KEY_SIZE];
	memcpy(aes_key, sha256_key, AES_KEY_SIZE);

	log_hex("aes_open: aes_key[%d]: %s", aes_key, AES_KEY_SIZE);

	aes_setkey_dec(&c->aes, aes_key, AES_KEY_SIZE << 3);

//    h->is_streamed = 1; // disable seek
	LOGI(3, "aes_open: finished opening");
	err: return ret;
}

static int64_t aes_seek(URLContext *h, int64_t pos, int whence) {
	AesContext *c = h->priv_data;
	LOGI(3, "aes_seek: trying to seek");
	switch (whence) {
	case SEEK_SET:
		LOGI(3, "aes_seek: pos: %"PRId64", SEEK_SET", pos);
		// The offset is set to offset bytes.
		c->reading_position = pos;
		break;

	case SEEK_CUR:
		LOGI(3, "aes_seek: pos: %"PRId64", SEEK_CUR", pos);
		// The offset is set to its current location plus offset bytes.
		c->reading_position += pos;
		break;

	case AVSEEK_SIZE:
		// Measuring file size
		LOGI(3, "aes_seek: AVSEEK_SIZE");
		if (c->stream_end >= 0) {
			LOGI(3, "aes_seek: already_measured_size: %"PRId64, c->stream_end);
			return c->stream_end;
		}
		c->stream_end = ffurl_seek(c->hd, 0, AVSEEK_SIZE);
		LOGI(3, "aes_seek: measured_size: %"PRId64, c->stream_end);
		return c->stream_end;

	case SEEK_END:
		LOGI(3, "aes_seek: pos: %d, SEEK_END", pos);
		// The offset is set to the size of the file plus offset bytes.
		if (c->stream_end < 0) {
			c->stream_end = ffurl_seek(c->hd, 0, AVSEEK_SIZE);
			if (c->stream_end < 0) {
				LOGE(2,
						"aes_seek: could not measure size, error: %"PRId64, c->stream_end);
				return c->stream_end;
			}
		}
		LOGI(3, "aes_seek: measured_size: %"PRId64, c->stream_end);
		c->reading_position = c->stream_end - pos;
		break;
	default:
		LOGE(1, "aes_seek: unknown whence: %d", whence);
		return -1;
	}
	LOGI(3, "aes_seek: reading_position: %" PRId64, c->reading_position);

	c->read_start_point = (c->reading_position / (int64_t) BUFFER_SIZE)
			* (int64_t) BUFFER_SIZE;
	c->read_end_point = c->read_start_point;
	LOGI(3, "aes_seek: read_start_point: %" PRId64, c->read_start_point);

	int64_t ret = ffurl_seek(c->hd, c->read_start_point, whence);
	LOGI(3, "aes_seek: return: %"PRId64, ret);
	if (ret < 0) {
		LOGE(1,
				"aes_seek: seeking error: %"PRId64", trying to seek: %"PRId64", whence: %d", ret, c->read_start_point, whence);
		return ret;
	}
	if (ret != c->read_start_point) {
		LOGE(1, "aes_seek: seeking fatal error: unknown state");
		return -2;
	}
	return c->reading_position;
}

static int aes_read(URLContext *h, uint8_t *buf, int size) {
	AesContext *c = h->priv_data;

	int buf_position = 0;
	int buf_left = size;
	int end = FALSE;
	LOGI(3, "aes_read started");

	while (buf_left > 0 && !end) {
		LOGI(3,
				"aes_read loop, read_position: %"PRId64", buf_left: %d", c->reading_position, buf_left);
		if (c->reading_position < c->read_start_point) {
			LOGE(1, "aes_read reading error");
			return -1;
		}

		while (c->reading_position >= c->read_end_point && !end) {
			LOGI(3,
					"aes_read read loop: current read_end_point %"PRId64, c->read_end_point);
			int64_t position = c->read_end_point;

			int decode_buf_left = BUFFER_SIZE;
			int encrypted_buffer_size = 0;
			while (decode_buf_left > 0 && !end) {
				int n = ffurl_read(c->hd,
						&(c->read_buff[encrypted_buffer_size]),
						decode_buf_left);

				if (n < 0)
					return n;

				if (n == 0)
					end = TRUE;

				decode_buf_left -= n;
				encrypted_buffer_size += n;
			}
			c->read_start_point = c->read_end_point;
			c->read_end_point += encrypted_buffer_size;

			// Inflight magic trick - LOL
			*(int *) &c->iv[0] = (int) (c->read_start_point >> 9);
			memset(&c->iv[4], 0, sizeof(c->iv) - 4);
			aes_crypt_cbc(&c->aes, AES_DECRYPT, encrypted_buffer_size, c->iv,
					c->read_buff, c->decoded_buff);
			LOGI(3, "aes_read enc: position: %"PRId64, position);
			log_hex("aes_read enc: encoded[%d]: %s", c->read_buff,
					encrypted_buffer_size);
			log_hex("aes_read enc: decoded[%d]: %s", c->decoded_buff,
					encrypted_buffer_size);
		}
		int delta = c->reading_position - c->read_start_point;
		int copy_size = c->read_end_point - c->reading_position;
		if (copy_size > buf_left)
			copy_size = buf_left;

		LOGI(10, "aes_read delta: %d, copy_size: %d", delta, copy_size);
		memcpy(&buf[buf_position], &c->decoded_buff[delta], copy_size);
		c->reading_position += copy_size;
		buf_left -= copy_size;
		buf_position += copy_size;
	}
	LOGI(3, "aes_read read bytes: %d", buf_position);
	log_hex("eas_read wrote to buffer[%d]: %s", buf, buf_position);
	LOGI(3, "aes_read write success");
	return buf_position;
}

static int aes_close(URLContext *h) {
	AesContext *c = h->priv_data;
	if (c->hd)
		ffurl_close(c->hd);
	return 0;
}

URLProtocol aes_protocol = { .name = "aes", .url_open = aes_open, .url_read =
		aes_read, .url_close = aes_close, .url_seek = aes_seek,
		.priv_data_size = sizeof(AesContext), .priv_data_class = &aes_class,
		.flags = URL_PROTOCOL_FLAG_NESTED_SCHEME, };

void register_aes_protocol() {
	ffurl_register_protocol(&aes_protocol, sizeof(aes_protocol));
}
