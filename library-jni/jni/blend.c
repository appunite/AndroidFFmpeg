/*
 * blend.c
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

#include "blend.h"
#include <android/log.h>

#define RGBA_IN(r, g, b, a, s)\
{\
    unsigned int v = ((const uint32_t *)(s))[0];\
    a = (v >> 24) & 0xff;\
    r = (v >> 16) & 0xff;\
    g = (v >> 8) & 0xff;\
    b = v & 0xff;\
}

#define RGB565_IN(r, g, b, s)\
{\
    unsigned int v = ((const uint16_t *)(s))[0];\
    r = (v >> 11) & 0x1f;\
    g = (v >> 5) & 0x3f;\
    b = v & 0x1f;\
}

#define RGB565_OUT(d, r, g, b)\
{\
    ((uint16_t *)(d))[0] = (r << 11) | (g << 5) | b;\
}

#define RGBA_OUT(d, r, g, b, a)\
{\
    ((uint32_t *)(d))[0] = (a << 24) | (r << 16) | (g << 8) | b;\
}

#define ALPHA_BLEND_RGB(color1, color2, alpha)\
	(((color1 * (0xff - alpha)) + (color2 * alpha))/0xff)

#define AR(c)  ( (c)>>24)
#define AG(c)  (((c)>>16)&0xFF)
#define AB(c)  (((c)>>8) &0xFF)
#define AA(c)  ((0xFF-c) &0xFF)

void blend_ass_image(AVPicture *dest, const ASS_Image *image, int imgw,
		int imgh, enum PixelFormat pixel_format) {
	uint8_t rgba_color[] = { AR(image->color), AG(image->color), AB(
			image->color), AA(image->color) };
	uint8_t rect_r, rect_g, rect_b, rect_a;
	int dest_r, dest_g, dest_b, dest_a;
	int x, y;
	uint32_t *dst2;
	uint8_t *src, *src2;
	uint8_t *dst = dest->data[0];

	if (pixel_format != PIX_FMT_RGBA)
		return;

	dst += image->dst_y * dest->linesize[0] + image->dst_x * 4;
	src = image->bitmap;
	for (y = 0; y < image->h; y++) {
		dst2 = (uint32_t *) dst;
		src2 = src;
		for (x = 0; x < image->w; x++) {
			uint8_t image_pixel = *(src2++);
			uint32_t *pixel = (dst2++);

			rect_r = image_pixel & rgba_color[0];
			rect_g = image_pixel & rgba_color[1];
			rect_b = image_pixel & rgba_color[2];
			rect_a = image_pixel & rgba_color[3];

			RGBA_IN(dest_r, dest_g, dest_b, dest_a, pixel);

			// write subtitle on the image
			dest_r = ALPHA_BLEND_RGB(dest_r, rect_r, rect_a);
			dest_g = ALPHA_BLEND_RGB(dest_g, rect_g, rect_a);
			dest_b = ALPHA_BLEND_RGB(dest_b, rect_b, rect_a);

			RGBA_OUT(pixel, dest_r, dest_g, dest_b, dest_a);
		}
		dst += dest->linesize[0];
		src += image->stride;
	}
}

void blend_subrect_rgba(AVPicture *dest, const AVSubtitleRect *rect, int imgw,
		int imgh, enum PixelFormat pixel_format) {
	int rect_r, rect_g, rect_b, rect_a;
	int dest_r, dest_g, dest_b, dest_a;
	uint32_t *pal;
	uint32_t *dst2;
	uint8_t *src, *src2;
	int x, y;
	uint8_t *dst = dest->data[0];

	if (pixel_format != PIX_FMT_RGBA)
		return;

	dst += rect->y * dest->linesize[0] + rect->x * 4;
	src = rect->pict.data[0];
	pal = (uint32_t *) rect->pict.data[1];

	for (y = 0; y < rect->h; y++) {
		dst2 = (uint32_t *) dst;
		src2 = src;
		for (x = 0; x < rect->w; x++) {
			uint32_t *rect_pixel = &pal[*(src2++)];
			uint32_t *pixel = (dst2++);

			// read subtitle rgba8888
			RGBA_IN(rect_r, rect_g, rect_b, rect_a, rect_pixel);

			RGBA_IN(dest_r, dest_g, dest_b, dest_a, pixel);

			// write subtitle on the image
			dest_r = ALPHA_BLEND_RGB(dest_r, rect_r, rect_a);
			dest_g = ALPHA_BLEND_RGB(dest_g, rect_g, rect_a);
			dest_b = ALPHA_BLEND_RGB(dest_b, rect_b, rect_a);

			RGBA_OUT(pixel, dest_r, dest_g, dest_b, dest_a);
		}
		dst += dest->linesize[0];
		src += rect->pict.linesize[0];
	}
}

