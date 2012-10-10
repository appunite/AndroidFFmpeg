/*
 * blend.h
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

#ifndef BLEND_H_
#define BLEND_H_

#include <libavcodec/avcodec.h>
#include <ass/ass.h>

void blend_ass_image(AVPicture *dest, const ASS_Image *image, int imgw,
		int imgh, enum PixelFormat pixel_format);
void blend_subrect_rgba(AVPicture *dest, const AVSubtitleRect *rect, int imgw,
		int imgh, enum PixelFormat pixel_format);

#endif /* BLEND_H_ */
