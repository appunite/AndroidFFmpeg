/* YUV-> RGB conversion code.
 *
 * Copyright (C) 2011 Robin Watts (robin@wss.co.uk) for Pinknoise
 * Productions Ltd.
 *
 * Licensed under the BSD license. See 'COPYING' for details of
 * (non-)warranty.
 *
 */

#ifndef YUV2RGB_H

#define YUV2RGB_H

#include <stdint.h>

extern const uint32_t yuv2rgb565_table[];
extern const uint32_t yuv2bgr565_table[];

void yuv420_2_rgb565(uint8_t  *dst_ptr,
               const uint8_t  *y_ptr,
               const uint8_t  *u_ptr,
               const uint8_t  *v_ptr,
                     int32_t   width,
                     int32_t   height,
                     int32_t   y_span,
                     int32_t   uv_span,
                     int32_t   dst_span,
               const uint32_t *tables,
                     int32_t   dither);

void yuv422_2_rgb565(uint8_t  *dst_ptr,
               const uint8_t  *y_ptr,
               const uint8_t  *u_ptr,
               const uint8_t  *v_ptr,
                     int32_t   width,
                     int32_t   height,
                     int32_t   y_span,
                     int32_t   uv_span,
                     int32_t   dst_span,
               const uint32_t *tables,
                     int32_t   dither);

void yuv444_2_rgb565(uint8_t  *dst_ptr,
               const uint8_t  *y_ptr,
               const uint8_t  *u_ptr,
               const uint8_t  *v_ptr,
                     int32_t   width,
                     int32_t   height,
                     int32_t   y_span,
                     int32_t   uv_span,
                     int32_t   dst_span,
               const uint32_t *tables,
                     int32_t   dither);

void yuv420_2_rgb888(uint8_t  *dst_ptr,
               const uint8_t  *y_ptr,
               const uint8_t  *u_ptr,
               const uint8_t  *v_ptr,
                     int32_t   width,
                     int32_t   height,
                     int32_t   y_span,
                     int32_t   uv_span,
                     int32_t   dst_span,
               const uint32_t *tables,
                     int32_t   dither);

void yuv422_2_rgb888(uint8_t  *dst_ptr,
               const uint8_t  *y_ptr,
               const uint8_t  *u_ptr,
               const uint8_t  *v_ptr,
                     int32_t   width,
                     int32_t   height,
                     int32_t   y_span,
                     int32_t   uv_span,
                     int32_t   dst_span,
               const uint32_t *tables,
                     int32_t   dither);

void yuv444_2_rgb888(uint8_t  *dst_ptr,
               const uint8_t  *y_ptr,
               const uint8_t  *u_ptr,
               const uint8_t  *v_ptr,
                     int32_t   width,
                     int32_t   height,
                     int32_t   y_span,
                     int32_t   uv_span,
                     int32_t   dst_span,
               const uint32_t *tables,
                     int32_t   dither);

void yuv420_2_rgb8888(uint8_t  *dst_ptr,
                const uint8_t  *y_ptr,
                const uint8_t  *u_ptr,
                const uint8_t  *v_ptr,
                      int32_t   width,
                      int32_t   height,
                      int32_t   y_span,
                      int32_t   uv_span,
                      int32_t   dst_span,
                const uint32_t *tables,
                      int32_t   dither);

void yuv422_2_rgb8888(uint8_t  *dst_ptr,
                const uint8_t  *y_ptr,
                const uint8_t  *u_ptr,
                const uint8_t  *v_ptr,
                      int32_t   width,
                      int32_t   height,
                      int32_t   y_span,
                      int32_t   uv_span,
                      int32_t   dst_span,
                const uint32_t *tables,
                      int32_t   dither);

void yuv444_2_rgb8888(uint8_t  *dst_ptr,
                const uint8_t  *y_ptr,
                const uint8_t  *u_ptr,
                const uint8_t  *v_ptr,
                      int32_t   width,
                      int32_t   height,
                      int32_t   y_span,
                      int32_t   uv_span,
                      int32_t   dst_span,
                const uint32_t *tables,
                      int32_t   dither);


#endif /* YUV2RGB_H */
