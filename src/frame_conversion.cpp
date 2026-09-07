/**
 * @file frame_conversion.cpp
 * @brief CPU I420 copy and SIMD-dispatched packed-pixel conversion.
 */
#include "frame_conversion.hpp"

#include "cpu_conversion_workers.hpp"

#if defined(MKVC_HAS_CPU_VP9) || defined(MKVC_HAS_CPU_AV1) || defined(MKVC_HAS_INTEL_ONEVPL) || \
    defined(MKVC_HAS_NVIDIA)
#include <libyuv/convert_from.h>
#include <libyuv/convert_from_argb.h>
#include <libyuv/planar_functions.h>
#endif

#include <algorithm>
#include <cstdint>
#include <vector>

namespace {

#if defined(MKVC_HAS_CPU_VP9) || defined(MKVC_HAS_CPU_AV1) || defined(MKVC_HAS_INTEL_ONEVPL) || \
    defined(MKVC_HAS_NVIDIA)
/** Convert one even-height I420 stripe through libyuv's runtime SIMD dispatch. */
int convert_i420_packed_stripe(const uint32_t format, const uint8_t* y, const int y_stride,
                               const uint8_t* u, const int u_stride, const uint8_t* v,
                               const int v_stride, uint8_t* destination,
                               const int destination_stride, const int width, const int height) {
    if (format == MKVC_PIXEL_FORMAT_BGR24) {
        return libyuv::I420ToRGB24(y, y_stride, u, u_stride, v, v_stride, destination,
                                   destination_stride, width, height);
    }
    if (format == MKVC_PIXEL_FORMAT_RGB24) {
        return libyuv::I420ToRAW(y, y_stride, u, u_stride, v, v_stride, destination,
                                 destination_stride, width, height);
    }
    return libyuv::I420ToARGB(y, y_stride, u, u_stride, v, v_stride, destination,
                              destination_stride, width, height);
}

/**
 * Convert large packed frames in independent chroma-aligned stripes.
 *
 * Every worker still uses libyuv's architecture-specific SIMD row kernels.
 * Splitting only large frames amortizes task creation while using multiple CPU
 * cores for the conversion that follows a multithreaded decoder.
 */
int convert_i420_packed(const uint32_t format, const uint8_t* y, const int y_stride,
                        const uint8_t* u, const int u_stride, const uint8_t* v, const int v_stride,
                        uint8_t* destination, const int destination_stride, const int width,
                        const int height) {
    constexpr int kParallelPixelThreshold = 1280 * 720;
    const size_t worker_count = static_cast<int64_t>(width) * height >= kParallelPixelThreshold
                                    ? mkvc::cpu_conversion_parallelism()
                                    : 1;
    if (worker_count == 1) {
        return convert_i420_packed_stripe(format, y, y_stride, u, u_stride, v, v_stride,
                                          destination, destination_stride, width, height);
    }

    const int rows_per_worker =
        ((height + static_cast<int>(worker_count) - 1) / static_cast<int>(worker_count) + 1) & ~1;
    std::vector<std::future<int>> operations;
    for (int row = rows_per_worker; row < height; row += rows_per_worker) {
        const int stripe_height = std::min(rows_per_worker, height - row);
        operations.emplace_back(mkvc::submit_cpu_conversion([=] {
            return convert_i420_packed_stripe(
                format, y + row * y_stride, y_stride, u + (row / 2) * u_stride, u_stride,
                v + (row / 2) * v_stride, v_stride, destination + row * destination_stride,
                destination_stride, width, stripe_height);
        }));
    }
    const int first_stripe_height = std::min(rows_per_worker, height);
    int result =
        convert_i420_packed_stripe(format, y, y_stride, u, u_stride, v, v_stride, destination,
                                   destination_stride, width, first_stripe_height);
    for (auto& operation : operations) result |= operation.get();
    return result;
}
#endif

}  // namespace

namespace mkvc {

mkvc_result copy_frame_to(const DecodedFrame& source, mkvc_mutable_frame_view& destination,
                          std::string& error) {
#if !defined(MKVC_HAS_CPU_VP9) && !defined(MKVC_HAS_CPU_AV1) && !defined(MKVC_HAS_INTEL_ONEVPL) && \
    !defined(MKVC_HAS_NVIDIA)
    (void)source;
    (void)destination;
    error = "CPU frame conversion was not built";
    return MKVC_ERROR_NOT_SUPPORTED;
#else
    if (destination.width != source.width || destination.height != source.height) {
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
            if (destination.planes[0] == nullptr || destination.planes[1] == nullptr ||
                destination.planes[2] == nullptr || destination.strides[0] < width ||
                destination.strides[1] < width / 2 || destination.strides[2] < width / 2) {
                error = "I420 destination planes or strides are invalid";
                return MKVC_ERROR_INVALID_ARGUMENT;
            }
            result =
                libyuv::I420Copy(y, source.strides[0], u, source.strides[1], v, source.strides[2],
                                 destination.planes[0], destination.strides[0],
                                 destination.planes[1], destination.strides[1],
                                 destination.planes[2], destination.strides[2], width, height);
            break;
        case MKVC_PIXEL_FORMAT_NV12:
            if (destination.planes[0] == nullptr || destination.planes[1] == nullptr ||
                destination.strides[0] < width || destination.strides[1] < width) {
                error = "NV12 destination planes or strides are invalid";
                return MKVC_ERROR_INVALID_ARGUMENT;
            }
            result =
                libyuv::I420ToNV12(y, source.strides[0], u, source.strides[1], v, source.strides[2],
                                   destination.planes[0], destination.strides[0],
                                   destination.planes[1], destination.strides[1], width, height);
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
            result = convert_i420_packed(
                destination.pixel_format, y, source.strides[0], u, source.strides[1], v,
                source.strides[2], destination.planes[0], destination.strides[0], width, height);
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
