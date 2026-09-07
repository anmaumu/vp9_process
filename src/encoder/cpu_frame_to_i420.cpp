#include "encoder/cpu_frame_to_i420.hpp"

#include <libyuv/convert.h>

#include <cstddef>
#include <cstring>

namespace mkvc::encoder {
namespace {

void copy_plane(uint8_t* destination, int destination_stride, const uint8_t* source,
                int source_stride, uint32_t width, uint32_t height) {
    for (uint32_t row = 0; row < height; ++row)
        std::memcpy(destination + static_cast<size_t>(row) * destination_stride,
                    source + static_cast<size_t>(row) * source_stride, width);
}

}  // namespace

mkvc_result convert_cpu_frame_to_i420(const mkvc_frame_view& frame, uint32_t width, uint32_t height,
                                      std::vector<uint8_t>& i420, const char* codec_name,
                                      std::string& error) {
    const size_t y_size = static_cast<size_t>(width) * height;
    const size_t chroma_size = y_size / 4;
    const size_t frame_size = y_size + chroma_size * 2;
    const std::string codec = codec_name ? codec_name : "CPU";
    if (i420.size() < frame_size) {
        error = codec + " CPU staging buffer is too small";
        return MKVC_ERROR_INTERNAL;
    }
    uint8_t* y = i420.data();
    uint8_t* u = y + y_size;
    uint8_t* v = u + chroma_size;
    int converted = 0;
    switch (frame.pixel_format) {
        case MKVC_PIXEL_FORMAT_I420:
            if (frame.planes[0] == nullptr || frame.planes[1] == nullptr ||
                frame.planes[2] == nullptr || frame.strides[0] < static_cast<int32_t>(width) ||
                frame.strides[1] < static_cast<int32_t>(width / 2) ||
                frame.strides[2] < static_cast<int32_t>(width / 2)) {
                error = "I420 requires three positive-stride planes";
                return MKVC_ERROR_INVALID_ARGUMENT;
            }
            copy_plane(y, static_cast<int>(width), frame.planes[0], frame.strides[0], width,
                       height);
            copy_plane(u, static_cast<int>(width / 2), frame.planes[1], frame.strides[1], width / 2,
                       height / 2);
            copy_plane(v, static_cast<int>(width / 2), frame.planes[2], frame.strides[2], width / 2,
                       height / 2);
            break;
        case MKVC_PIXEL_FORMAT_NV12:
            if (frame.planes[0] == nullptr || frame.planes[1] == nullptr ||
                frame.strides[0] < static_cast<int32_t>(width) ||
                frame.strides[1] < static_cast<int32_t>(width)) {
                error = "NV12 requires Y and interleaved UV positive-stride planes";
                return MKVC_ERROR_INVALID_ARGUMENT;
            }
            converted = libyuv::NV12ToI420(
                frame.planes[0], frame.strides[0], frame.planes[1], frame.strides[1], y,
                static_cast<int>(width), u, static_cast<int>(width / 2), v,
                static_cast<int>(width / 2), static_cast<int>(width), static_cast<int>(height));
            break;
        case MKVC_PIXEL_FORMAT_BGR24:
        case MKVC_PIXEL_FORMAT_RGB24:
        case MKVC_PIXEL_FORMAT_BGRA32: {
            const uint32_t channels = frame.pixel_format == MKVC_PIXEL_FORMAT_BGRA32 ? 4u : 3u;
            if (frame.planes[0] == nullptr ||
                frame.strides[0] < static_cast<int32_t>(width * channels)) {
                error = "packed RGB input has an invalid pointer or stride";
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
            break;
        }
        default:
            error = "unsupported " + codec + " input pixel format";
            return MKVC_ERROR_NOT_SUPPORTED;
    }
    if (converted != 0) {
        error = "libyuv failed to convert the " + codec + " input frame";
        return MKVC_ERROR_INTERNAL;
    }
    return MKVC_OK;
}

}  // namespace mkvc::encoder
