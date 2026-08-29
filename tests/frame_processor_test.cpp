#include "cpu_vp9_decoder.hpp"
#include "frame_processor.hpp"

#include <cassert>
#include <cstdint>
#include <memory>
#include <string>

namespace {
mkvc::DecodedFrame make_frame(uint32_t width, uint32_t height) {
    mkvc::DecodedFrame frame;
    frame.width = width;
    frame.height = height;
    frame.pts_ns = 123456;
    const size_t y_size = static_cast<size_t>(width) * height;
    const size_t uv_size = static_cast<size_t>(width / 2) * (height / 2);
    frame.offsets = {0, y_size, y_size + uv_size};
    frame.strides = {static_cast<int32_t>(width), static_cast<int32_t>(width / 2),
                     static_cast<int32_t>(width / 2)};
    frame.pixels.resize(y_size + 2 * uv_size, 128);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            frame.pixels[y * width + x] = static_cast<uint8_t>(16 + x + y * width);
        }
    }
    return frame;
}
}

int main() {
    const auto source = make_frame(8, 6);
    mkvc_frame_process_config config{};
    config.struct_size = sizeof(config);
    config.struct_version = 1;
    config.backend = MKVC_BACKEND_CPU;
    config.crop_x = 2;
    config.crop_y = 2;
    config.crop_width = 4;
    config.crop_height = 2;
    config.output_width = 8;
    config.output_height = 8;
    config.fit = MKVC_FRAME_FIT_CONTAIN;
    config.rotation = MKVC_FRAME_ROTATE_90;
    config.flip_horizontal = 1;
    config.background_rgba = 0x000000ffu;
    std::unique_ptr<mkvc::DecodedFrame> output;
    std::string error;
    assert(mkvc::process_frame_cpu(source, config, output, error) == MKVC_OK);
    assert(output && output->width == 8 && output->height == 8);
    assert(output->pts_ns == source.pts_ns);
    assert(output->pixels[0] == 16);  // limited-range black letterbox

    config.crop_x = 1;
    assert(mkvc::process_frame_cpu(source, config, output, error) ==
           MKVC_ERROR_INVALID_ARGUMENT);
    config.crop_x = 2;
    config.backend = MKVC_BACKEND_NVIDIA;
    assert(mkvc::process_frame_cpu(source, config, output, error) ==
           MKVC_ERROR_NOT_SUPPORTED);
    return 0;
}
