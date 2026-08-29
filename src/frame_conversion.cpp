#include "frame_conversion.hpp"

#if defined(MKVC_HAS_CPU_VP9) || defined(MKVC_HAS_CPU_AV1) || \
    defined(MKVC_HAS_INTEL_ONEVPL) || defined(MKVC_HAS_NVIDIA)
#include <libyuv/convert_from.h>
#include <libyuv/convert_from_argb.h>
#include <libyuv/planar_functions.h>
#endif

#include <cstdint>

namespace mkvc {

mkvc_result copy_frame_to(const DecodedFrame& source,
                          mkvc_mutable_frame_view& destination,
                          std::string& error) {
#if !defined(MKVC_HAS_CPU_VP9) && !defined(MKVC_HAS_CPU_AV1) && \
    !defined(MKVC_HAS_INTEL_ONEVPL) && !defined(MKVC_HAS_NVIDIA)
    (void)source;
    (void)destination;
    error = "CPU frame conversion was not built";
    return MKVC_ERROR_NOT_SUPPORTED;
#else
    if (destination.width != source.width ||
        destination.height != source.height) {
        error = "destination dimensions do not match decoded frame";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    const uint8_t* y = source.pixels.data() + source.offsets[0];
    const uint8_t* u = source.pixels.data() + source.offsets[1];
    const uint8_t* v = source.pixels.data() + source.offsets[2];
    const int width = static_cast<int>(source.width);
    const int height = static_cast<int>(source.height);
    int result = -1;

    switch (destination.pixel_format) {
        case MKVC_PIXEL_FORMAT_I420:
            if (destination.planes[0] == nullptr ||
                destination.planes[1] == nullptr ||
                destination.planes[2] == nullptr ||
                destination.strides[0] < width ||
                destination.strides[1] < width / 2 ||
                destination.strides[2] < width / 2) {
                error = "I420 destination planes or strides are invalid";
                return MKVC_ERROR_INVALID_ARGUMENT;
            }
            result = libyuv::I420Copy(
                y, source.strides[0], u, source.strides[1],
                v, source.strides[2], destination.planes[0],
                destination.strides[0], destination.planes[1],
                destination.strides[1], destination.planes[2],
                destination.strides[2], width, height);
            break;
        case MKVC_PIXEL_FORMAT_NV12:
            if (destination.planes[0] == nullptr ||
                destination.planes[1] == nullptr ||
                destination.strides[0] < width ||
                destination.strides[1] < width) {
                error = "NV12 destination planes or strides are invalid";
                return MKVC_ERROR_INVALID_ARGUMENT;
            }
            result = libyuv::I420ToNV12(
                y, source.strides[0], u, source.strides[1],
                v, source.strides[2], destination.planes[0],
                destination.strides[0], destination.planes[1],
                destination.strides[1], width, height);
            break;
        case MKVC_PIXEL_FORMAT_BGR24:
        case MKVC_PIXEL_FORMAT_RGB24:
        case MKVC_PIXEL_FORMAT_BGRA32: {
            const int bytes_per_pixel =
                destination.pixel_format == MKVC_PIXEL_FORMAT_BGRA32 ? 4 : 3;
            if (destination.planes[0] == nullptr ||
                destination.strides[0] < width * bytes_per_pixel) {
                error = "packed destination pointer or stride is invalid";
                return MKVC_ERROR_INVALID_ARGUMENT;
            }
            if (destination.pixel_format == MKVC_PIXEL_FORMAT_BGR24) {
                result = libyuv::I420ToRGB24(
                    y, source.strides[0], u, source.strides[1],
                    v, source.strides[2], destination.planes[0],
                    destination.strides[0], width, height);
            } else if (destination.pixel_format == MKVC_PIXEL_FORMAT_RGB24) {
                result = libyuv::I420ToRAW(
                    y, source.strides[0], u, source.strides[1],
                    v, source.strides[2], destination.planes[0],
                    destination.strides[0], width, height);
            } else {
                result = libyuv::I420ToARGB(
                    y, source.strides[0], u, source.strides[1],
                    v, source.strides[2], destination.planes[0],
                    destination.strides[0], width, height);
            }
            break;
        }
        default:
            error = "unsupported destination pixel format";
            return MKVC_ERROR_NOT_SUPPORTED;
    }
    if (result != 0) {
        error = "libyuv failed to convert decoded frame";
        return MKVC_ERROR_INTERNAL;
    }
    destination.pts = source.pts_ns;
    return MKVC_OK;
#endif
}

}  // namespace mkvc
