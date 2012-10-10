#ifndef CONVERT_H_
#define CONVERT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <libyuv/basic_types.h>

enum __FilterMode {
  __kFilterNone = 0,  // Point sample; Fastest.
  __kFilterBilinear = 1,  // Faster than box, but lower quality scaling down.
  __kFilterBox = 2  // Highest quality.
};

	int __I420ToARGB(const uint8* src_y, int src_stride_y,
			const uint8* src_u, int src_stride_u,
			const uint8* src_v, int src_stride_v,
			uint8* dst_argb, int dst_stride_argb,
			int width, int height);

	int __NV12ToARGB(const uint8* src_y, int src_stride_y,
            const uint8* src_uv, int src_stride_uv,
            uint8* dst_argb, int dst_stride_argb,
            int width, int height);
	int __NV21ToARGB(const uint8* src_y, int src_stride_y,
	            const uint8* src_uv, int src_stride_uv,
	            uint8* dst_argb, int dst_stride_argb,
	            int width, int height);
	int __BGRAToARGB(const uint8* src_frame, int src_stride_frame,
	               uint8* dst_argb, int dst_stride_argb,
	               int width, int height);
	int __ARGBCopy(const uint8* src_argb, int src_stride_argb,
	             uint8* dst_argb, int dst_stride_argb,
	             int width, int height);

	int __ARGBScale(const uint8* src_argb, int src_stride_argb,
	              int src_width, int src_height,
	              uint8* dst_argb, int dst_stride_argb,
	              int dst_width, int dst_height,
	              enum __FilterMode filtering);

	int __ARGBToRGBA(const uint8* src_frame, int src_stride_frame,
	               uint8* dst_argb, int dst_stride_argb,
	               int width, int height);
#ifdef __cplusplus
}
#endif

#endif /* CONVERT_H_ */
