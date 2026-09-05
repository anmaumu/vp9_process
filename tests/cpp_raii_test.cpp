#include "mkvcodec/mkvcodec.hpp"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>

namespace {
void release_external(void* opaque) {
    ++*static_cast<unsigned*>(opaque);
}
}

int main(int argc, char** argv) {
    assert(argc == 2);
    mkvcodec::GpuResourcePool gpu_pool(1);
    auto gpu_slot = gpu_pool.acquire();
    assert(gpu_slot.slot_index() == 0 && gpu_slot.generation() == 1);
    assert(!gpu_pool.try_acquire().has_value());
    gpu_slot.reset();
    auto gpu_slot_again = gpu_pool.try_acquire();
    assert(gpu_slot_again && gpu_slot_again->generation() == 2);
    auto gpu_stats = gpu_pool.stats();
    assert(gpu_stats.capacity == 1 && gpu_stats.in_use == 1 &&
           gpu_stats.peak_in_use == 1);
    assert(gpu_stats.acquisitions == 2 &&
           gpu_stats.rejected_acquisitions == 1);
    gpu_slot_again.reset();
    constexpr uint32_t width = 64;
    constexpr uint32_t height = 48;
    const std::string output = argv[1];
    std::filesystem::remove(output);

    mkvcodec::CpuFramePool original_pool(
        MKVC_PIXEL_FORMAT_I420, width, height, 1);
    mkvcodec::CpuFramePool pool(std::move(original_pool));
    assert(!original_pool.native_handle());
    auto buffer = pool.acquire();
    const auto first_desc = buffer.descriptor();
    auto view = buffer.view();
    for (uint32_t row = 0; row < height; ++row) {
        for (uint32_t column = 0; column < width; ++column) {
            view.planes[0][row * view.strides[0] + column] = 96;
        }
    }
    for (uint32_t row = 0; row < height / 2; ++row) {
        for (uint32_t column = 0; column < width / 2; ++column) {
            view.planes[1][row * view.strides[1] + column] = 128;
            view.planes[2][row * view.strides[2] + column] = 128;
        }
    }
    assert(!pool.try_acquire().has_value());

    mkvc_encoder_config config{};
    config.struct_size = sizeof(config);
    config.struct_version = 1;
    config.output_path_utf8 = output.c_str();
    config.codec = MKVC_CODEC_VP9;
    config.backend = MKVC_BACKEND_CPU;
    config.width = width;
    config.height = height;
    config.fps_num = 30;
    config.fps_den = 1;
    config.quality = 32;
    config.queue_size = 1;
    mkvcodec::Encoder encoder(config);
    auto submission = encoder.submit(buffer, 0);
    buffer.reset();
    submission.wait(5000);
    assert(submission.status() == MKVC_SUBMISSION_COMPLETE);
    submission.reset();

    auto recycled = pool.acquire(5000);
    assert(recycled.descriptor().generation > first_desc.generation);
    recycled.reset();
    encoder.close();
    const auto metrics = encoder.metrics();
    assert(metrics.accepted_frames == 1 && metrics.completed_frames == 1);
    assert(std::filesystem::exists(output));
    assert(std::filesystem::file_size(output) > 0);

    mkvc_decoder_config decoder_config{};
    decoder_config.struct_size = sizeof(decoder_config);
    decoder_config.struct_version = 1;
    decoder_config.input_path_utf8 = output.c_str();
    decoder_config.codec = MKVC_CODEC_VP9;
    decoder_config.backend = MKVC_BACKEND_CPU;
    mkvcodec::Decoder decoder(decoder_config);
    auto decoded = decoder.read();
    assert(decoded.has_value());
    const auto decoded_view = decoded->view();
    assert(decoded_view.pixel_format == MKVC_PIXEL_FORMAT_I420);
    assert(decoded_view.width == width && decoded_view.height == height);
    decoded.reset();
    assert(!decoder.read().has_value());
    decoder.close();
    assert(decoder.metrics().completed_frames == 1);

    unsigned external_releases = 0;
    mkvc_gpu_external_frame_config external{};
    external.struct_size = sizeof(external);
    external.struct_version = 1;
    external.frame.struct_size = sizeof(external.frame);
    external.frame.struct_version = 1;
    external.frame.backend = MKVC_BACKEND_NVIDIA;
    external.frame.memory_type = MKVC_GPU_MEMORY_CUDA_POINTER;
    external.frame.device_id = 1;
    external.frame.generation = 1;
    external.frame.pixel_format = MKVC_PIXEL_FORMAT_NV12;
    external.frame.width = width;
    external.frame.height = height;
    external.frame.plane_count = 2;
    external.frame.pitches[0] = width;
    external.frame.pitches[1] = width;
    external.frame.plane_offsets[1] = width * height;
    external.native_handle.struct_size = sizeof(external.native_handle);
    external.native_handle.struct_version = 1;
    external.native_handle.type = MKVC_GPU_NATIVE_CUDA_POINTER;
    external.native_handle.borrowed = 1;
    external.native_handle.device_id = 1;
    external.native_handle.generation = 1;
    external.native_handle.handles[0] = 0x1000;
    external.native_handle.handles[1] = 0x2000;
    external.release = release_external;
    external.user_data = &external_releases;
    auto external_frame = mkvcodec::GpuFrame::import_external(external);
    assert(external_frame.descriptor().width == width);
    assert(external_frame.completion_status() == MKVC_GPU_COMPLETION_COMPLETE);
    external_frame.reset();
    assert(external_releases == 1);
    return 0;
}
