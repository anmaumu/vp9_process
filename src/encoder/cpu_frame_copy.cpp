#include "cpu_frame_copy.hpp"

#include <cstring>

namespace mkvc::encoder {
namespace {

bool copy_plane(const uint8_t* source, int32_t source_stride, uint32_t row_bytes, uint32_t rows,
                std::vector<uint8_t>& destination, int32_t& destination_stride) {
    if (source == nullptr || source_stride < static_cast<int32_t>(row_bytes)) {
        return false;
    }
    destination_stride = static_cast<int32_t>(row_bytes);
    destination.resize(static_cast<size_t>(row_bytes) * rows);
    for (uint32_t row = 0; row < rows; ++row) {
        std::memcpy(destination.data() + static_cast<size_t>(row) * row_bytes,
                    source + static_cast<size_t>(row) * source_stride, row_bytes);
    }
    return true;
}

}  // namespace

mkvc_frame_view OwnedFrame::view() const {
    mkvc_frame_view result{};
    result.struct_size = sizeof(result);
    result.struct_version = 1;
    result.pixel_format = pixel_format;
    result.width = width;
    result.height = height;
    result.pts = pts;
    for (size_t index = 0; index < planes.size(); ++index) {
        result.planes[index] = planes[index].empty() ? nullptr : planes[index].data();
        result.strides[index] = strides[index];
    }
    return result;
}

mkvc_result validate_borrowed_frame(const mkvc_frame_view& frame, std::string& error) {
    if (frame.width == 0 || frame.height == 0 || (frame.width & 1u) != 0 ||
        (frame.height & 1u) != 0) {
        error = "borrowed input dimensions must be positive and even";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    const auto valid_plane = [&frame](size_t index, uint32_t row_bytes) {
        return frame.planes[index] != nullptr &&
               frame.strides[index] >= static_cast<int32_t>(row_bytes);
    };
    bool valid = false;
    switch (frame.pixel_format) {
        case MKVC_PIXEL_FORMAT_I420:
            valid = valid_plane(0, frame.width) && valid_plane(1, frame.width / 2) &&
                    valid_plane(2, frame.width / 2);
            break;
        case MKVC_PIXEL_FORMAT_NV12:
            valid = valid_plane(0, frame.width) && valid_plane(1, frame.width);
            break;
        case MKVC_PIXEL_FORMAT_BGR24:
        case MKVC_PIXEL_FORMAT_RGB24:
            valid = valid_plane(0, frame.width * 3);
            break;
        case MKVC_PIXEL_FORMAT_BGRA32:
            valid = valid_plane(0, frame.width * 4);
            break;
        default:
            error = "unsupported borrowed input pixel format";
            return MKVC_ERROR_NOT_SUPPORTED;
    }
    if (!valid) {
        error = "borrowed input has an invalid plane or stride";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    return MKVC_OK;
}

mkvc_result own_frame(const mkvc_frame_view& source, OwnedFrame& destination, std::string& error) {
    destination.pixel_format = source.pixel_format;
    destination.width = source.width;
    destination.height = source.height;
    destination.pts = source.pts;
    bool valid = false;
    switch (source.pixel_format) {
        case MKVC_PIXEL_FORMAT_I420:
            valid = copy_plane(source.planes[0], source.strides[0], source.width, source.height,
                               destination.planes[0], destination.strides[0]) &&
                    copy_plane(source.planes[1], source.strides[1], source.width / 2,
                               source.height / 2, destination.planes[1], destination.strides[1]) &&
                    copy_plane(source.planes[2], source.strides[2], source.width / 2,
                               source.height / 2, destination.planes[2], destination.strides[2]);
            break;
        case MKVC_PIXEL_FORMAT_NV12:
            valid = copy_plane(source.planes[0], source.strides[0], source.width, source.height,
                               destination.planes[0], destination.strides[0]) &&
                    copy_plane(source.planes[1], source.strides[1], source.width, source.height / 2,
                               destination.planes[1], destination.strides[1]);
            break;
        case MKVC_PIXEL_FORMAT_BGR24:
        case MKVC_PIXEL_FORMAT_RGB24:
            valid = copy_plane(source.planes[0], source.strides[0], source.width * 3, source.height,
                               destination.planes[0], destination.strides[0]);
            break;
        case MKVC_PIXEL_FORMAT_BGRA32:
            valid = copy_plane(source.planes[0], source.strides[0], source.width * 4, source.height,
                               destination.planes[0], destination.strides[0]);
            break;
        default:
            error = "unsupported asynchronous input pixel format";
            return MKVC_ERROR_NOT_SUPPORTED;
    }
    if (!valid) {
        error = "asynchronous input has an invalid plane or stride";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    return MKVC_OK;
}

}  // namespace mkvc::encoder
