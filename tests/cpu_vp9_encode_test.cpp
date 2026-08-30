#include "mkvcodec/mkvc.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cmath>
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

    // Keep the shared fixture inside current NVDEC minimum dimensions so the
    // same bitstream exercises CPU and NVIDIA decoder integrations.
    constexpr uint32_t width = 160;
    constexpr uint32_t height = 128;
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
    config.queue_size = 2;

    mkvc_encoder* encoder = nullptr;
    mkvc_encoder_config failing_config = config;
    const std::string impossible_path = output_path + "/missing/output.webm";
    failing_config.output_path_utf8 = impossible_path.c_str();
    assert(mkvc_encoder_create(&failing_config, &encoder) != MKVC_OK);
    assert(encoder == nullptr);
    assert(mkvc_get_last_error()[0] != '\0');

    require_ok(mkvc_encoder_create(&config, &encoder));
    assert(encoder != nullptr);
    mkvc_copy_policy copy_policy{};
    copy_policy.struct_size = sizeof(copy_policy);
    copy_policy.struct_version = 1;
    copy_policy.allow_gpu_copy = 1;
    copy_policy.allow_cpu_copy = 1;
    require_ok(mkvc_encoder_set_copy_policy(encoder, &copy_policy));
    mkvc_pipeline_metrics invalid_metrics{};
    invalid_metrics.struct_size = sizeof(invalid_metrics) - 1;
    invalid_metrics.struct_version = 1;
    assert(mkvc_encoder_get_metrics(encoder, &invalid_metrics) ==
           MKVC_ERROR_INVALID_ARGUMENT);

    mkvc_submission* async_borrowed = nullptr;
    mkvc_frame_view invalid_borrowed{};
    invalid_borrowed.struct_size = sizeof(invalid_borrowed);
    invalid_borrowed.struct_version = 1;
    invalid_borrowed.pixel_format = MKVC_PIXEL_FORMAT_I420;
    invalid_borrowed.width = width;
    invalid_borrowed.height = height;
    assert(mkvc_encoder_submit_frame_borrowed(
               encoder, &invalid_borrowed, &async_borrowed) ==
           MKVC_ERROR_INVALID_ARGUMENT);
    assert(async_borrowed == nullptr);

    mkvc_frame_view borrowed_probe{};
    borrowed_probe.struct_size = sizeof(borrowed_probe);
    borrowed_probe.struct_version = 1;
    borrowed_probe.pixel_format = MKVC_PIXEL_FORMAT_I420;
    borrowed_probe.width = width;
    borrowed_probe.height = height;
    borrowed_probe.planes[0] = image.data();
    borrowed_probe.planes[1] = image.data() + y_size;
    borrowed_probe.planes[2] = image.data() + y_size + uv_size;
    borrowed_probe.strides[0] = width;
    borrowed_probe.strides[1] = width / 2;
    borrowed_probe.strides[2] = width / 2;
    assert(mkvc_encoder_write_frame_borrowed(encoder, &borrowed_probe) ==
           MKVC_ERROR_NOT_SUPPORTED);

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
        if (index == frame_count - 1) {
            require_ok(mkvc_encoder_submit_frame_borrowed(
                encoder, &frame, &async_borrowed));
            assert(async_borrowed != nullptr);
            uint32_t submission_status = 99;
            require_ok(mkvc_submission_query(
                async_borrowed, &submission_status));
            assert(submission_status == MKVC_SUBMISSION_PENDING ||
                   submission_status == MKVC_SUBMISSION_COMPLETE);
            require_ok(mkvc_submission_wait(async_borrowed, 5000));
            require_ok(mkvc_submission_query(
                async_borrowed, &submission_status));
            assert(submission_status == MKVC_SUBMISSION_COMPLETE);
            mkvc_submission_release(async_borrowed);
            async_borrowed = nullptr;
        } else {
            const mkvc_result try_result =
                mkvc_encoder_try_write_frame(encoder, &frame);
            if (try_result == MKVC_WOULD_BLOCK) {
                require_ok(mkvc_encoder_write_frame(encoder, &frame));
            } else {
                require_ok(try_result);
            }
        }
        if (index == 0) {
            assert(mkvc_encoder_set_copy_policy(encoder, &copy_policy) ==
                   MKVC_ERROR_INVALID_STATE);
        }
    }

    require_ok(mkvc_encoder_flush(encoder));
    require_ok(mkvc_encoder_close(encoder));
    require_ok(mkvc_encoder_close(encoder));
    mkvc_pipeline_metrics encoder_metrics{};
    encoder_metrics.struct_size = sizeof(encoder_metrics);
    encoder_metrics.struct_version = 1;
    require_ok(mkvc_encoder_get_metrics(encoder, &encoder_metrics));
    assert(encoder_metrics.accepted_frames == frame_count);
    assert(encoder_metrics.completed_frames == frame_count);
    assert(encoder_metrics.queue_capacity == 2);
    assert(encoder_metrics.peak_queue_depth > 0 &&
           encoder_metrics.peak_queue_depth <= 2);
    assert(encoder_metrics.backend_time_ns > 0);
    assert(encoder_metrics.copy_path == MKVC_COPY_PATH_CPU);

    mkvc_frame_view closed_frame{};
    closed_frame.struct_size = sizeof(closed_frame);
    closed_frame.struct_version = 1;
    assert(mkvc_encoder_write_frame(encoder, &closed_frame) ==
           MKVC_ERROR_INVALID_STATE);
    assert(mkvc_get_last_error()[0] != '\0');
    mkvc_encoder_destroy(encoder);

    const std::string borrowed_output = output_path + ".borrowed.webm";
    std::filesystem::remove(borrowed_output);
    mkvc_encoder_config borrowed_config = config;
    borrowed_config.output_path_utf8 = borrowed_output.c_str();
    borrowed_config.queue_size = 0;
    require_ok(mkvc_encoder_create(&borrowed_config, &encoder));
    borrowed_probe.pts = 0;
    require_ok(mkvc_encoder_write_frame_borrowed(encoder, &borrowed_probe));
    // The synchronous call has consumed its input; caller memory is reusable.
    image[0] ^= 0xff;
    require_ok(mkvc_encoder_close(encoder));
    mkvc_encoder_destroy(encoder);
    assert(std::filesystem::exists(borrowed_output));
    assert(std::filesystem::file_size(borrowed_output) > 0);

    assert(std::filesystem::exists(output_path));
    assert(std::filesystem::file_size(output_path) > 0);

    mkvc_decoder_config decoder_config{};
    decoder_config.struct_size = sizeof(decoder_config);
    decoder_config.struct_version = 1;
    decoder_config.input_path_utf8 = output_path.c_str();
    decoder_config.codec = MKVC_CODEC_VP9;
    decoder_config.backend = MKVC_BACKEND_CPU;
    decoder_config.prefetch = 2;

    mkvc_decoder* decoder = nullptr;
    require_ok(mkvc_decoder_create(&decoder_config, &decoder));
    invalid_metrics.struct_size = sizeof(invalid_metrics);
    invalid_metrics.struct_version = 0;
    assert(mkvc_decoder_get_metrics(decoder, &invalid_metrics) ==
           MKVC_ERROR_INVALID_ARGUMENT);
    uint32_t decoded_count = 0;
    int64_t previous_pts = -1;
    double squared_error = 0.0;
    uint64_t sample_count = 0;
    while (true) {
        mkvc_frame* decoded = nullptr;
        const mkvc_result result = mkvc_decoder_read(decoder, &decoded);
        if (result == MKVC_END_OF_STREAM) {
            assert(decoded == nullptr);
            break;
        }
        require_ok(result);
        assert(decoded != nullptr);
        mkvc_frame_view view{};
        view.struct_size = sizeof(view);
        view.struct_version = 1;
        require_ok(mkvc_frame_get_view(decoded, &view));
        assert(view.pixel_format == MKVC_PIXEL_FORMAT_I420);
        assert(view.width == width && view.height == height);
        assert(view.planes[0] != nullptr && view.planes[1] != nullptr &&
               view.planes[2] != nullptr);
        assert(view.pts > previous_pts);
        previous_pts = view.pts;
        for (uint32_t row = 0; row < height; ++row) {
            for (uint32_t column = 0; column < width; ++column) {
                const int expected = static_cast<uint8_t>(
                    (column * 3 + row * 2 + decoded_count * 7) & 0xff);
                const int actual = view.planes[0][row * view.strides[0] + column];
                const double difference = static_cast<double>(actual - expected);
                squared_error += difference * difference;
                ++sample_count;
            }
        }
        mkvc_frame_retain(decoded);
        mkvc_frame_release(decoded);
        mkvc_frame_release(decoded);
        ++decoded_count;
    }
    assert(decoded_count == frame_count);
    const double mse = squared_error / static_cast<double>(sample_count);
    const double psnr = 10.0 * std::log10(255.0 * 255.0 / mse);
    assert(psnr >= 28.0);
    require_ok(mkvc_decoder_close(decoder));
    require_ok(mkvc_decoder_close(decoder));
    mkvc_pipeline_metrics decoder_metrics{};
    decoder_metrics.struct_size = sizeof(decoder_metrics);
    decoder_metrics.struct_version = 1;
    require_ok(mkvc_decoder_get_metrics(decoder, &decoder_metrics));
    assert(decoder_metrics.accepted_frames == frame_count);
    assert(decoder_metrics.completed_frames == frame_count);
    assert(decoder_metrics.queue_capacity == 2);
    assert(decoder_metrics.peak_queue_depth > 0 &&
           decoder_metrics.peak_queue_depth <= 2);
    assert(decoder_metrics.backend_time_ns > 0);
    assert(decoder_metrics.copy_path == MKVC_COPY_PATH_CPU);
    mkvc_decoder_destroy(decoder);
    std::filesystem::remove(borrowed_output);
    return 0;
}
