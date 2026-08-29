#include "mkvcodec/mkvc.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <string>
#include <vector>

namespace {

void set_failure_hook(const char* value) {
#if defined(_WIN32)
    _putenv_s("MKVC_TEST_ENCODER_FAIL_AFTER", value == nullptr ? "" : value);
#else
    if (value == nullptr) {
        unsetenv("MKVC_TEST_ENCODER_FAIL_AFTER");
    } else {
        setenv("MKVC_TEST_ENCODER_FAIL_AFTER", value, 1);
    }
#endif
}

mkvc_encoder_config config_for(const std::string& path) {
    mkvc_encoder_config config{};
    config.struct_size = sizeof(config);
    config.struct_version = 1;
    config.output_path_utf8 = path.c_str();
    config.codec = MKVC_CODEC_VP9;
    config.backend = MKVC_BACKEND_CPU;
    config.width = 64;
    config.height = 48;
    config.fps_num = 30;
    config.fps_den = 1;
    config.quality = 32;
    config.queue_size = 1;
    return config;
}

}  // namespace

int main() {
    const auto directory = std::filesystem::temp_directory_path();
    const std::string failed_path =
        (directory / "mkvc-async-injected-failure.webm").string();
    const std::string recovered_path =
        (directory / "mkvc-async-recovered.webm").string();
    std::filesystem::remove(failed_path);
    std::filesystem::remove(recovered_path);

    constexpr uint32_t width = 64;
    constexpr uint32_t height = 48;
    const size_t y_size = width * height;
    const size_t uv_size = width * height / 4;
    std::vector<uint8_t> pixels(y_size + uv_size * 2, 128);
    mkvc_frame_view frame{};
    frame.struct_size = sizeof(frame);
    frame.struct_version = 1;
    frame.pixel_format = MKVC_PIXEL_FORMAT_I420;
    frame.width = width;
    frame.height = height;
    frame.planes[0] = pixels.data();
    frame.planes[1] = pixels.data() + y_size;
    frame.planes[2] = pixels.data() + y_size + uv_size;
    frame.strides[0] = width;
    frame.strides[1] = width / 2;
    frame.strides[2] = width / 2;
    frame.pts = -1;

    set_failure_hook("0");
    mkvc_encoder* failed = nullptr;
    auto failed_config = config_for(failed_path);
    if (mkvc_encoder_create(&failed_config, &failed) != MKVC_OK) return 1;
    std::vector<std::future<mkvc_result>> writers;
    for (uint32_t index = 0; index < 8; ++index) {
        writers.push_back(std::async(std::launch::async, [failed, &frame] {
            return mkvc_encoder_write_frame(failed, &frame);
        }));
    }
    bool observed_failure = false;
    for (auto& writer : writers) {
        if (writer.wait_for(std::chrono::seconds(5)) !=
            std::future_status::ready) return 2;
        const mkvc_result result = writer.get();
        if (result == MKVC_ERROR_IO) observed_failure = true;
        else if (result != MKVC_OK) return 3;
    }
    if (!observed_failure || mkvc_encoder_close(failed) != MKVC_ERROR_IO) return 4;
    mkvc_pipeline_metrics metrics{};
    metrics.struct_size = sizeof(metrics);
    metrics.struct_version = 1;
    if (mkvc_encoder_get_metrics(failed, &metrics) != MKVC_OK ||
        metrics.completed_frames != 0 || metrics.queue_capacity != 1 ||
        metrics.peak_queue_depth > 1) return 5;
    mkvc_encoder_destroy(failed);

    set_failure_hook(nullptr);
    mkvc_encoder* recovered = nullptr;
    auto recovered_config = config_for(recovered_path);
    if (mkvc_encoder_create(&recovered_config, &recovered) != MKVC_OK ||
        mkvc_encoder_write_frame(recovered, &frame) != MKVC_OK ||
        mkvc_encoder_close(recovered) != MKVC_OK) return 6;
    mkvc_encoder_destroy(recovered);
    if (!std::filesystem::exists(recovered_path) ||
        std::filesystem::file_size(recovered_path) == 0) return 7;
    std::filesystem::remove(failed_path);
    std::filesystem::remove(recovered_path);
    return 0;
}
