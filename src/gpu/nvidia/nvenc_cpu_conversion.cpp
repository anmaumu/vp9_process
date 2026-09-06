#include "nvenc_cpu_conversion.hpp"

#include <libyuv/convert.h>
#include <libyuv/planar_functions.h>

#include <cstddef>
#include <cstring>

namespace mkvc::gpu::nvidia {
namespace {

void copy_plane(uint8_t* destination, int destination_stride, const uint8_t* source,
                int source_stride, uint32_t width, uint32_t height) {
    for (uint32_t row = 0; row < height; ++row)
        std::memcpy(destination + static_cast<size_t>(row) * destination_stride,
                    source + static_cast<size_t>(row) * source_stride, width);
}

}  // namespace

mkvc_result convert_nvenc_input_to_nv12(const mkvc_frame_view& frame, uint32_t width,
                                        uint32_t height, std::vector<uint8_t>& i420,
                                        std::vector<uint8_t>& nv12, std::string& error) {
    const size_t y_size = static_cast<size_t>(width) * height;
    const size_t chroma = y_size / 4;
    const size_t frame_size = y_size + chroma * 2;
    if (i420.size() < frame_size || nv12.size() < frame_size) {
        error = "NVIDIA CPU staging buffers are too small";
        return MKVC_ERROR_INTERNAL;
    }
    uint8_t* y = i420.data();
    uint8_t* u = y + y_size;
    uint8_t* v = u + chroma;
    uint8_t* nv_y = nv12.data();
    uint8_t* nv_uv = nv_y + y_size;
    int converted = 0;
    switch (frame.pixel_format) {
        case MKVC_PIXEL_FORMAT_NV12:
            if (frame.planes[0] == nullptr || frame.planes[1] == nullptr ||
                frame.strides[0] < static_cast<int32_t>(width) ||
                frame.strides[1] < static_cast<int32_t>(width)) {
                error = "NV12 requires valid Y and UV planes";
                return MKVC_ERROR_INVALID_ARGUMENT;
            }
            copy_plane(nv_y, static_cast<int>(width), frame.planes[0], frame.strides[0], width,
                       height);
            copy_plane(nv_uv, static_cast<int>(width), frame.planes[1], frame.strides[1], width,
                       height / 2);
            return MKVC_OK;
        case MKVC_PIXEL_FORMAT_I420:
            if (frame.planes[0] == nullptr || frame.planes[1] == nullptr ||
                frame.planes[2] == nullptr || frame.strides[0] < static_cast<int32_t>(width) ||
                frame.strides[1] < static_cast<int32_t>(width / 2) ||
                frame.strides[2] < static_cast<int32_t>(width / 2)) {
                error = "I420 requires three valid planes";
                return MKVC_ERROR_INVALID_ARGUMENT;
            }
            converted = libyuv::I420ToNV12(
                frame.planes[0], frame.strides[0], frame.planes[1], frame.strides[1],
                frame.planes[2], frame.strides[2], nv_y, static_cast<int>(width), nv_uv,
                static_cast<int>(width), static_cast<int>(width), static_cast<int>(height));
            break;
        case MKVC_PIXEL_FORMAT_BGR24:
        case MKVC_PIXEL_FORMAT_RGB24:
        case MKVC_PIXEL_FORMAT_BGRA32: {
            const uint32_t channels = frame.pixel_format == MKVC_PIXEL_FORMAT_BGRA32 ? 4u : 3u;
            if (frame.planes[0] == nullptr ||
                frame.strides[0] < static_cast<int32_t>(width * channels)) {
                error = "packed RGB input has an invalid plane or stride";
                return MKVC_ERROR_INVALID_ARGUMENT;
            }
            if (frame.pixel_format == MKVC_PIXEL_FORMAT_BGR24)
                converted = libyuv::RGB24ToI420(frame.planes[0], frame.strides[0], y, width, u,
                                                width / 2, v, width / 2, width, height);
            else if (frame.pixel_format == MKVC_PIXEL_FORMAT_RGB24)
                converted = libyuv::RAWToI420(frame.planes[0], frame.strides[0], y, width, u,
                                              width / 2, v, width / 2, width, height);
            else
                converted = libyuv::ARGBToI420(frame.planes[0], frame.strides[0], y, width, u,
                                               width / 2, v, width / 2, width, height);
            if (converted == 0)
                converted = libyuv::I420ToNV12(y, width, u, width / 2, v, width / 2, nv_y, width,
                                               nv_uv, width, width, height);
            break;
        }
        default:
            error = "unsupported NVIDIA encoder input format";
            return MKVC_ERROR_NOT_SUPPORTED;
    }
    if (converted != 0) {
        error = "libyuv failed to convert NVIDIA encoder input";
        return MKVC_ERROR_INTERNAL;
    }
    return MKVC_OK;
}

}  // namespace mkvc::gpu::nvidia
