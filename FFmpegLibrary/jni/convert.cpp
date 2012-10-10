#include <convert.h>
#include <libyuv.h>
#include <libyuv/convert_from.h>
#include <libyuv/scale.h>

extern "C" {
	int __I420ToARGB(const uint8* src_y, int src_stride_y,
			const uint8* src_u, int src_stride_u,
			const uint8* src_v, int src_stride_v,
			uint8* dst_argb, int dst_stride_argb,
			int width, int height) {
		return libyuv::I420ToARGB(src_y,src_stride_y,
				src_u, src_stride_u,
				src_v, src_stride_v,
				dst_argb, dst_stride_argb,
				width, height);
	}

	int __NV12ToARGB(const uint8* src_y, int src_stride_y,
            const uint8* src_uv, int src_stride_uv,
            uint8* dst_argb, int dst_stride_argb,
            int width, int height) {
			return libyuv::NV12ToARGB(src_y, src_stride_y,
			               src_uv, src_stride_uv,
			               dst_argb, dst_stride_argb,
			               width, height);
		}

	int __NV21ToARGB(const uint8* src_y, int src_stride_y,
	            const uint8* src_uv, int src_stride_uv,
	            uint8* dst_argb, int dst_stride_argb,
	            int width, int height) {
				return libyuv::NV21ToARGB(src_y, src_stride_y,
				               src_uv, src_stride_uv,
				               dst_argb, dst_stride_argb,
				               width, height);
			}

	int __BGRAToARGB(const uint8* src_frame, int src_stride_frame,
	               uint8* dst_argb, int dst_stride_argb,
	               int width, int height) {
		return libyuv::BGRAToARGB(src_frame, src_stride_frame,
		               dst_argb, dst_stride_argb,
		               width, height);
	}

	int __ARGBCopy(const uint8* src_argb, int src_stride_argb,
	             uint8* dst_argb, int dst_stride_argb,
	             int width, int height) {
		return libyuv::ARGBCopy(src_argb, src_stride_argb,
		             dst_argb, dst_stride_argb,
		             width, height);
	}

	int __ARGBScale(const uint8* src_argb, int src_stride_argb,
	              int src_width, int src_height,
	              uint8* dst_argb, int dst_stride_argb,
	              int dst_width, int dst_height,
	              enum __FilterMode filtering) {
		libyuv::FilterMode filterMode = static_cast<libyuv::FilterMode>(filtering);
		return libyuv::ARGBScale(src_argb, src_stride_argb,
		              src_width, src_height,
		              dst_argb, dst_stride_argb,
		              dst_width, dst_height,
		              filterMode);
	}

	int __ARGBToRGBA(const uint8* src_frame, int src_stride_frame,
            uint8* dst_argb, int dst_stride_argb,
            int width, int height) {
		return libyuv::ARGBToRGBA(src_frame, src_stride_frame,
			               dst_argb, dst_stride_argb,
			               width, height);
	}
}
