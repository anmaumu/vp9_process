#include "mkvcodec/mkvc.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace {

void require_ok(mkvc_result result) {
    if (result != MKVC_OK) {
        assert(mkvc_get_last_error()[0] != '\0');
    }
    assert(result == MKVC_OK);
}

}  // namespace

int main(int argc, char** argv) {
    assert(argc == 2);
    const std::string output_path = argv[1];
    std::filesystem::remove(output_path);

    constexpr uint32_t width = 64;
    constexpr uint32_t height = 48;
    constexpr uint32_t frame_count = 30;
    const size_t y_size = width * height;
    const size_t uv_size = width * height / 4;
    std::vector<uint8_t> image(y_size + 2 * uv_size);

    mkvc_encoder_config config{};
    config.struct_size = sizeof(config);
    config.struct_version = 1;
    config.output_path_utf8 = output_path.c_str();
    config.codec = MKVC_CODEC_VP9;
    config.backend = MKVC_BACKEND_CPU;
    config.width = width;
    config.height = height;
    config.fps_num = 30;
    config.fps_den = 1;
    config.quality = 32;

    mkvc_encoder* encoder = nullptr;
    mkvc_encoder_config failing_config = config;
    const std::string impossible_path = output_path + "/missing/output.webm";
    failing_config.output_path_utf8 = impossible_path.c_str();
    assert(mkvc_encoder_create(&failing_config, &encoder) != MKVC_OK);
    assert(encoder == nullptr);
    assert(mkvc_get_last_error()[0] != '\0');

    require_ok(mkvc_encoder_create(&config, &encoder));
    assert(encoder != nullptr);

    for (uint32_t index = 0; index < frame_count; ++index) {
        for (uint32_t row = 0; row < height; ++row) {
            for (uint32_t column = 0; column < width; ++column) {
                image[row * width + column] = static_cast<uint8_t>(
                    (column * 3 + row * 2 + index * 7) & 0xff);
            }
        }
        std::fill(image.begin() + y_size, image.begin() + y_size + uv_size,
                  static_cast<uint8_t>(96 + index));
        std::fill(image.begin() + y_size + uv_size, image.end(),
                  static_cast<uint8_t>(160 - index));

        mkvc_frame_view frame{};
        frame.struct_size = sizeof(frame);
        frame.struct_version = 1;
        frame.pixel_format = MKVC_PIXEL_FORMAT_I420;
        frame.width = width;
        frame.height = height;
        frame.planes[0] = image.data();
        frame.planes[1] = image.data() + y_size;
        frame.planes[2] = image.data() + y_size + uv_size;
        frame.strides[0] = width;
        frame.strides[1] = width / 2;
        frame.strides[2] = width / 2;
        frame.pts = -1;
        require_ok(mkvc_encoder_write_frame(encoder, &frame));
    }

    require_ok(mkvc_encoder_flush(encoder));
    require_ok(mkvc_encoder_close(encoder));
    require_ok(mkvc_encoder_close(encoder));

    mkvc_frame_view closed_frame{};
    closed_frame.struct_size = sizeof(closed_frame);
    closed_frame.struct_version = 1;
    assert(mkvc_encoder_write_frame(encoder, &closed_frame) ==
           MKVC_ERROR_INVALID_STATE);
    assert(mkvc_get_last_error()[0] != '\0');
    mkvc_encoder_destroy(encoder);

    assert(std::filesystem::exists(output_path));
    assert(std::filesystem::file_size(output_path) > 0);
    return 0;
}
