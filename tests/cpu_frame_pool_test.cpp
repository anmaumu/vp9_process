#include <cassert>
#include <cstdint>
#include <filesystem>
#include <string>

#include "mkvcodec/mkvc.h"

namespace {

void require_ok(mkvc_result result) {
    if (result != MKVC_OK) assert(mkvc_get_last_error()[0] != '\0');
    assert(result == MKVC_OK);
}

}  // namespace

int main(int argc, char** argv) {
    assert(argc == 2);
    constexpr uint32_t width = 64;
    constexpr uint32_t height = 48;

    mkvc_cpu_frame_pool_config pool_config{};
    pool_config.struct_size = sizeof(pool_config);
    pool_config.struct_version = 1;
    pool_config.pixel_format = MKVC_PIXEL_FORMAT_I420;
    pool_config.width = width;
    pool_config.height = height;
    pool_config.capacity = 1;

    mkvc_cpu_frame_pool* pool = nullptr;
    mkvc_cpu_frame_pool_config invalid_config = pool_config;
    invalid_config.capacity = 0;
    assert(mkvc_cpu_frame_pool_create(&invalid_config, &pool) == MKVC_ERROR_INVALID_ARGUMENT);
    assert(pool == nullptr);
    require_ok(mkvc_cpu_frame_pool_create(&pool_config, &pool));
    mkvc_cpu_frame_pool_options invalid_options{};
    invalid_options.struct_size = sizeof(invalid_options);
    invalid_options.struct_version = 1;
    invalid_options.memory_mode = MKVC_CPU_MEMORY_PAGEABLE;
    invalid_options.reserved = 1;
    mkvc_cpu_frame_pool* invalid_pool = reinterpret_cast<mkvc_cpu_frame_pool*>(1);
    assert(mkvc_cpu_frame_pool_create_ex(&pool_config, &invalid_options, &invalid_pool) ==
           MKVC_ERROR_INVALID_ARGUMENT);
    assert(invalid_pool == nullptr);
    mkvc_cpu_buffer* first = nullptr;
    require_ok(mkvc_cpu_frame_pool_acquire(pool, 0, &first));

    mkvc_cpu_buffer_desc first_desc{};
    first_desc.struct_size = sizeof(first_desc);
    first_desc.struct_version = 1;
    require_ok(mkvc_cpu_buffer_get_desc(first, &first_desc));
    assert(first_desc.pixel_format == MKVC_PIXEL_FORMAT_I420);
    assert(first_desc.width == width && first_desc.height == height);
    assert(first_desc.plane_count == 3 && first_desc.generation == 1);

    mkvc_mutable_frame_view view{};
    view.struct_size = sizeof(view);
    view.struct_version = 1;
    require_ok(mkvc_cpu_buffer_get_view(first, &view));
    for (uint32_t row = 0; row < height; ++row) {
        for (uint32_t column = 0; column < width; ++column) {
            view.planes[0][row * view.strides[0] + column] =
                static_cast<uint8_t>((row + column) & 0xffu);
        }
    }
    for (uint32_t row = 0; row < height / 2; ++row) {
        for (uint32_t column = 0; column < width / 2; ++column) {
            view.planes[1][row * view.strides[1] + column] = 96;
            view.planes[2][row * view.strides[2] + column] = 160;
        }
    }

    mkvc_cpu_buffer* unavailable = reinterpret_cast<mkvc_cpu_buffer*>(1);
    assert(mkvc_cpu_frame_pool_acquire(pool, 0, &unavailable) == MKVC_WOULD_BLOCK);
    assert(unavailable == nullptr);

    const std::string output = argv[1];
    std::filesystem::remove(output);
    mkvc_encoder_config encoder_config{};
    encoder_config.struct_size = sizeof(encoder_config);
    encoder_config.struct_version = 1;
    encoder_config.output_path_utf8 = output.c_str();
    encoder_config.codec = MKVC_CODEC_VP9;
    encoder_config.backend = MKVC_BACKEND_CPU;
    encoder_config.width = width;
    encoder_config.height = height;
    encoder_config.fps_num = 30;
    encoder_config.fps_den = 1;
    encoder_config.quality = 32;
    encoder_config.queue_size = 1;
    mkvc_encoder* encoder = nullptr;
    require_ok(mkvc_encoder_create(&encoder_config, &encoder));

    mkvc_submission* submission = nullptr;
    require_ok(mkvc_encoder_submit_cpu_buffer(encoder, first, 0, &submission));
    mkvc_cpu_buffer_release(first);
    first = nullptr;
    require_ok(mkvc_submission_wait(submission, 5000));
    uint32_t status = MKVC_SUBMISSION_PENDING;
    require_ok(mkvc_submission_query(submission, &status));
    assert(status == MKVC_SUBMISSION_COMPLETE);
    mkvc_submission_release(submission);

    mkvc_cpu_buffer* second = nullptr;
    require_ok(mkvc_cpu_frame_pool_acquire(pool, 5000, &second));
    mkvc_cpu_buffer_desc second_desc{};
    second_desc.struct_size = sizeof(second_desc);
    second_desc.struct_version = 1;
    require_ok(mkvc_cpu_buffer_get_desc(second, &second_desc));
    assert(second_desc.generation > first_desc.generation);

    mkvc_cpu_frame_pool_stats stats{};
    stats.struct_size = sizeof(stats);
    stats.struct_version = 1;
    require_ok(mkvc_cpu_frame_pool_get_stats(pool, &stats));
    assert(stats.capacity == 1 && stats.in_use == 1 && stats.peak_in_use == 1);
    assert(stats.memory_mode == MKVC_CPU_MEMORY_PAGEABLE);
    assert(stats.allocation_bytes == width * height * 3 / 2);
    assert(stats.acquisitions == 2 && stats.rejected_acquisitions == 1);
    assert(stats.lease_time_ns > 0 && stats.peak_lease_time_ns > 0);
    mkvc_cpu_frame_pool_stats invalid_stats{};
    invalid_stats.struct_size = sizeof(invalid_stats) - 1;
    invalid_stats.struct_version = 1;
    assert(mkvc_cpu_frame_pool_get_stats(pool, &invalid_stats) == MKVC_ERROR_INVALID_ARGUMENT);

    // The lease owns the pool allocation after its public pool owner is gone.
    mkvc_cpu_frame_pool_destroy(pool);
    pool = nullptr;
    view = {};
    view.struct_size = sizeof(view);
    view.struct_version = 1;
    require_ok(mkvc_cpu_buffer_get_view(second, &view));
    assert(view.planes[0] != nullptr);
    mkvc_cpu_buffer_release(second);

    mkvc_cpu_frame_pool_options locked_options{};
    locked_options.struct_size = sizeof(locked_options);
    locked_options.struct_version = 1;
    locked_options.memory_mode = MKVC_CPU_MEMORY_PAGE_LOCKED;
    mkvc_cpu_frame_pool* locked_pool = nullptr;
    require_ok(mkvc_cpu_frame_pool_create_ex(&pool_config, &locked_options, &locked_pool));
    stats = {};
    stats.struct_size = sizeof(stats);
    stats.struct_version = 1;
    require_ok(mkvc_cpu_frame_pool_get_stats(locked_pool, &stats));
    assert(stats.memory_mode == MKVC_CPU_MEMORY_PAGE_LOCKED);
    assert(stats.allocation_bytes == width * height * 3 / 2);
    assert(stats.page_locked_bytes >= stats.allocation_bytes);
    mkvc_cpu_frame_pool_destroy(locked_pool);

    require_ok(mkvc_encoder_close(encoder));
    mkvc_encoder_destroy(encoder);
    assert(std::filesystem::exists(output));
    assert(std::filesystem::file_size(output) > 0);
    return 0;
}
