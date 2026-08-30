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

void set_delay_hook(const char* value) {
#if defined(_WIN32)
    _putenv_s("MKVC_TEST_ENCODER_DELAY_MS", value == nullptr ? "" : value);
#else
    if (value == nullptr) {
        unsetenv("MKVC_TEST_ENCODER_DELAY_MS");
    } else {
        setenv("MKVC_TEST_ENCODER_DELAY_MS", value, 1);
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
    const std::string canceled_path =
        (directory / "mkvc-async-canceled.webm").string();
    std::filesystem::remove(failed_path);
    std::filesystem::remove(recovered_path);
    std::filesystem::remove(canceled_path);

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

    const std::string submission_failed_path =
        (directory / "mkvc-async-submission-failure.webm").string();
    std::filesystem::remove(submission_failed_path);
    mkvc_encoder* submission_failed = nullptr;
    auto submission_failed_config = config_for(submission_failed_path);
    if (mkvc_encoder_create(&submission_failed_config, &submission_failed) !=
        MKVC_OK) return 8;
    mkvc_submission* failed_submission = nullptr;
    if (mkvc_encoder_submit_frame_borrowed(
            submission_failed, &frame, &failed_submission) != MKVC_OK ||
        failed_submission == nullptr) return 9;
    if (mkvc_submission_wait(failed_submission, 5000) != MKVC_ERROR_IO)
        return 10;
    uint32_t failed_status = MKVC_SUBMISSION_PENDING;
    if (mkvc_submission_query(failed_submission, &failed_status) != MKVC_OK ||
        failed_status != MKVC_SUBMISSION_FAILED) return 11;
    mkvc_submission_release(failed_submission);
    if (mkvc_encoder_close(submission_failed) != MKVC_ERROR_IO) return 12;
    mkvc_encoder_destroy(submission_failed);

    set_failure_hook(nullptr);
    set_delay_hook("250");
    mkvc_encoder* canceled = nullptr;
    auto canceled_config = config_for(canceled_path);
    canceled_config.queue_size = 2;
    if (mkvc_encoder_create(&canceled_config, &canceled) != MKVC_OK) return 13;
    std::vector<mkvc_submission*> canceled_submissions(3, nullptr);
    for (auto& submission : canceled_submissions) {
        if (mkvc_encoder_submit_frame_borrowed(
                canceled, &frame, &submission) != MKVC_OK) return 14;
    }
    if (mkvc_encoder_cancel(canceled) != MKVC_OK ||
        mkvc_encoder_write_frame(canceled, &frame) != MKVC_ERROR_CANCELLED ||
        mkvc_encoder_flush(canceled) != MKVC_ERROR_CANCELLED) return 15;
    uint32_t canceled_count = 0;
    for (auto* submission : canceled_submissions) {
        const mkvc_result result = mkvc_submission_wait(submission, 5000);
        if (result == MKVC_ERROR_CANCELLED) ++canceled_count;
        else if (result != MKVC_OK) return 16;
        uint32_t status = MKVC_SUBMISSION_PENDING;
        if (mkvc_submission_query(submission, &status) != MKVC_OK) return 17;
        if (result == MKVC_ERROR_CANCELLED &&
            status != MKVC_SUBMISSION_CANCELLED) return 18;
        mkvc_submission_release(submission);
    }
    if (canceled_count == 0 || mkvc_encoder_cancel(canceled) != MKVC_OK ||
        mkvc_encoder_close(canceled) != MKVC_OK) return 19;
    mkvc_encoder_destroy(canceled);
    set_delay_hook(nullptr);

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
    std::filesystem::remove(submission_failed_path);
    std::filesystem::remove(canceled_path);
    return 0;
}
