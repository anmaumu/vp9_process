#include "frame_processor_i420.hpp"

#if defined(MKVC_HAS_CPU_VP9) || defined(MKVC_HAS_CPU_AV1) || defined(MKVC_HAS_INTEL_ONEVPL) || \
    defined(MKVC_HAS_NVIDIA)
#include <libyuv/planar_functions.h>
#include <libyuv/rotate.h>
#include <libyuv/scale.h>
#endif

#include <algorithm>
#include <cstdint>
#include <limits>

namespace mkvc::frame_processor_detail {

#if defined(MKVC_HAS_CPU_VP9) || defined(MKVC_HAS_CPU_AV1) || defined(MKVC_HAS_INTEL_ONEVPL) || \
    defined(MKVC_HAS_NVIDIA)

namespace {

uint8_t* plane(DecodedFrame& frame, size_t index) {
    return frame.pixels.data() + frame.offsets[index];
}

const uint8_t* plane(const DecodedFrame& frame, size_t index) {
    return frame.pixels.data() + frame.offsets[index];
}

}  // namespace

bool even_nonzero(uint32_t value) { return value != 0 && (value & 1u) == 0; }

std::unique_ptr<DecodedFrame> allocate_i420(uint32_t width, uint32_t height, int64_t pts_ns) {
    if (!even_nonzero(width) || !even_nonzero(height) ||
        width > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) ||
        height > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
        return nullptr;
    }
    const size_t y_size = static_cast<size_t>(width) * height;
    const size_t uv_size = static_cast<size_t>(width / 2) * (height / 2);
    if (y_size > std::numeric_limits<size_t>::max() - 2 * uv_size) return nullptr;
    auto frame = std::make_unique<DecodedFrame>();
    frame->width = width;
    frame->height = height;
    frame->pts_ns = pts_ns;
    frame->offsets = {0, y_size, y_size + uv_size};
    frame->strides = {static_cast<int32_t>(width), static_cast<int32_t>(width / 2),
                      static_cast<int32_t>(width / 2)};
    frame->pixels.resize(y_size + 2 * uv_size);
    return frame;
}

int copy_i420(const DecodedFrame& source, DecodedFrame& destination, uint32_t src_x, uint32_t src_y,
              uint32_t dst_x, uint32_t dst_y) {
    const uint32_t width = std::min(source.width - src_x, destination.width - dst_x);
    const uint32_t height = std::min(source.height - src_y, destination.height - dst_y);
    return libyuv::I420Copy(
        plane(source, 0) + src_y * source.strides[0] + src_x, source.strides[0],
        plane(source, 1) + (src_y / 2) * source.strides[1] + src_x / 2, source.strides[1],
        plane(source, 2) + (src_y / 2) * source.strides[2] + src_x / 2, source.strides[2],
        plane(destination, 0) + dst_y * destination.strides[0] + dst_x, destination.strides[0],
        plane(destination, 1) + (dst_y / 2) * destination.strides[1] + dst_x / 2,
        destination.strides[1],
        plane(destination, 2) + (dst_y / 2) * destination.strides[2] + dst_x / 2,
        destination.strides[2], static_cast<int>(width), static_cast<int>(height));
}

std::unique_ptr<DecodedFrame> crop_frame(const DecodedFrame& source, uint32_t x, uint32_t y,
                                         uint32_t width, uint32_t height) {
    auto result = allocate_i420(width, height, source.pts_ns);
    if (!result || copy_i420(source, *result, x, y) != 0) return nullptr;
    return result;
}

std::unique_ptr<DecodedFrame> rotate_frame(const DecodedFrame& source, uint32_t rotation) {
    const bool swaps = rotation == 90 || rotation == 270;
    auto result = allocate_i420(swaps ? source.height : source.width,
                                swaps ? source.width : source.height, source.pts_ns);
    if (!result) return nullptr;
    libyuv::RotationMode mode = libyuv::kRotate0;
    if (rotation == 90)
        mode = libyuv::kRotate90;
    else if (rotation == 180)
        mode = libyuv::kRotate180;
    else if (rotation == 270)
        mode = libyuv::kRotate270;
    const int rc = libyuv::I420Rotate(
        plane(source, 0), source.strides[0], plane(source, 1), source.strides[1], plane(source, 2),
        source.strides[2], plane(*result, 0), result->strides[0], plane(*result, 1),
        result->strides[1], plane(*result, 2), result->strides[2], static_cast<int>(source.width),
        static_cast<int>(source.height), mode);
    return rc == 0 ? std::move(result) : nullptr;
}

std::unique_ptr<DecodedFrame> mirror_frame(const DecodedFrame& source) {
    auto result = allocate_i420(source.width, source.height, source.pts_ns);
    if (!result) return nullptr;
    const int rc = libyuv::I420Mirror(
        plane(source, 0), source.strides[0], plane(source, 1), source.strides[1], plane(source, 2),
        source.strides[2], plane(*result, 0), result->strides[0], plane(*result, 1),
        result->strides[1], plane(*result, 2), result->strides[2], static_cast<int>(source.width),
        static_cast<int>(source.height));
    return rc == 0 ? std::move(result) : nullptr;
}

std::unique_ptr<DecodedFrame> scale_frame(const DecodedFrame& source, uint32_t width,
                                          uint32_t height) {
    auto result = allocate_i420(width, height, source.pts_ns);
    if (!result) return nullptr;
    const int rc = libyuv::I420Scale(
        plane(source, 0), source.strides[0], plane(source, 1), source.strides[1], plane(source, 2),
        source.strides[2], static_cast<int>(source.width), static_cast<int>(source.height),
        plane(*result, 0), result->strides[0], plane(*result, 1), result->strides[1],
        plane(*result, 2), result->strides[2], static_cast<int>(width), static_cast<int>(height),
        libyuv::kFilterBilinear);
    return rc == 0 ? std::move(result) : nullptr;
}

void fill_background(DecodedFrame& frame, uint32_t rgba) {
    const int r = static_cast<int>((rgba >> 24) & 0xffu);
    const int g = static_cast<int>((rgba >> 16) & 0xffu);
    const int b = static_cast<int>((rgba >> 8) & 0xffu);
    const uint8_t y =
        static_cast<uint8_t>(std::clamp(((66 * r + 129 * g + 25 * b + 128) >> 8) + 16, 0, 255));
    const uint8_t u =
        static_cast<uint8_t>(std::clamp(((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128, 0, 255));
    const uint8_t v =
        static_cast<uint8_t>(std::clamp(((112 * r - 94 * g - 18 * b + 128) >> 8) + 128, 0, 255));
    libyuv::SetPlane(plane(frame, 0), frame.strides[0], frame.width, frame.height, y);
    libyuv::SetPlane(plane(frame, 1), frame.strides[1], frame.width / 2, frame.height / 2, u);
    libyuv::SetPlane(plane(frame, 2), frame.strides[2], frame.width / 2, frame.height / 2, v);
}

uint32_t even_floor(uint64_t value) { return static_cast<uint32_t>(value & ~uint64_t{1}); }

#endif

}  // namespace mkvc::frame_processor_detail
