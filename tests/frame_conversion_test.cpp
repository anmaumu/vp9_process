/**
 * @file frame_conversion_test.cpp
 * @brief Pixel-exact serial and parallel I420 packed-conversion regression.
 */
#include "frame_conversion.hpp"

#include <libyuv/convert_argb.h>

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

namespace {

void verify_packed_conversion(const uint32_t width, const uint32_t height, const uint32_t format,
                              const uint32_t conversion_threads) {
    mkvc::DecodedFrame source;
    source.width = width;
    source.height = height;
    source.pts_ns = 123456789;
    const size_t y_size = static_cast<size_t>(width) * height;
    const size_t uv_size = static_cast<size_t>(width / 2) * (height / 2);
    source.pixels.resize(y_size + 2 * uv_size);
    source.offsets = {0, y_size, y_size + uv_size};
    source.strides = {static_cast<int32_t>(width), static_cast<int32_t>(width / 2),
                      static_cast<int32_t>(width / 2)};
    for (uint32_t row = 0; row < height; ++row) {
        for (uint32_t column = 0; column < width; ++column) {
            source.pixels[row * width + column] =
                static_cast<uint8_t>(16 + (row * 3 + column * 5) % 220);
        }
    }
    for (size_t index = 0; index < uv_size; ++index) {
        source.pixels[source.offsets[1] + index] = static_cast<uint8_t>(64 + index % 128);
        source.pixels[source.offsets[2] + index] = static_cast<uint8_t>(192 - index % 128);
    }

    const int bytes_per_pixel = format == MKVC_PIXEL_FORMAT_BGRA32 ? 4 : 3;
    const int stride = static_cast<int>(width) * bytes_per_pixel + 7;
    std::vector<uint8_t> actual(static_cast<size_t>(stride) * height, 0xa5);
    std::vector<uint8_t> expected(actual.size(), 0xa5);
    mkvc_mutable_frame_view destination{};
    destination.struct_size = sizeof(destination);
    destination.struct_version = 1;
    destination.pixel_format = format;
    destination.width = width;
    destination.height = height;
    destination.planes[0] = actual.data();
    destination.strides[0] = stride;
    std::string error;
    assert(mkvc::copy_frame_to(source, destination, error, conversion_threads) == MKVC_OK);
    assert(destination.pts == source.pts_ns);

    const uint8_t* y = source.pixels.data();
    const uint8_t* u = source.pixels.data() + source.offsets[1];
    const uint8_t* v = source.pixels.data() + source.offsets[2];
    int result = -1;
    if (format == MKVC_PIXEL_FORMAT_BGR24) {
        result = libyuv::I420ToRGB24(y, source.strides[0], u, source.strides[1], v,
                                     source.strides[2], expected.data(), stride,
                                     static_cast<int>(width), static_cast<int>(height));
    } else if (format == MKVC_PIXEL_FORMAT_RGB24) {
        result = libyuv::I420ToRAW(y, source.strides[0], u, source.strides[1], v, source.strides[2],
                                   expected.data(), stride, static_cast<int>(width),
                                   static_cast<int>(height));
    } else {
        result = libyuv::I420ToARGB(y, source.strides[0], u, source.strides[1], v,
                                    source.strides[2], expected.data(), stride,
                                    static_cast<int>(width), static_cast<int>(height));
    }
    assert(result == 0);
    assert(actual == expected);
}

}  // namespace

int main() {
    for (const uint32_t format :
         {MKVC_PIXEL_FORMAT_BGR24, MKVC_PIXEL_FORMAT_RGB24, MKVC_PIXEL_FORMAT_BGRA32}) {
        verify_packed_conversion(34, 18, format, 0);
        for (const uint32_t conversion_threads : {0u, 1u, 2u, 4u}) {
            verify_packed_conversion(1280, 722, format, conversion_threads);
        }
    }
    return 0;
}
