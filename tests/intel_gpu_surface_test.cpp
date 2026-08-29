#include "mkvcodec/mkvc.h"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2 || !std::filesystem::exists(argv[1])) return 77;
    mkvc_decoder_config config{};
    config.struct_size = sizeof(config);
    config.struct_version = 1;
    config.input_path_utf8 = argv[1];
    config.codec = MKVC_CODEC_VP9;
    config.backend = MKVC_BACKEND_INTEL;
    config.prefetch = 0;
    mkvc_decoder* decoder = nullptr;
    const mkvc_result created = mkvc_decoder_create(&config, &decoder);
    if (created == MKVC_ERROR_NOT_SUPPORTED) return 77;
    if (created != MKVC_OK) {
        std::cerr << mkvc_get_last_error() << '\n';
        return 1;
    }
    mkvc_gpu_frame* frame = nullptr;
    const mkvc_result read = mkvc_decoder_read_gpu(decoder, &frame);
    if (read != MKVC_OK) {
        std::cerr << mkvc_get_last_error() << '\n';
        mkvc_decoder_destroy(decoder);
        return 2;
    }
    assert(frame != nullptr);
    assert(mkvc_gpu_frame_wait(frame, 5000) == MKVC_OK);
    mkvc_gpu_frame_desc desc{};
    desc.struct_size = sizeof(desc);
    desc.struct_version = 1;
    assert(mkvc_gpu_frame_get_desc(frame, &desc) == MKVC_OK);
    assert(desc.backend == MKVC_BACKEND_INTEL);
    assert(desc.memory_type == MKVC_GPU_MEMORY_VA_SURFACE);
    assert(desc.pixel_format == MKVC_PIXEL_FORMAT_NV12 ||
           desc.pixel_format == MKVC_PIXEL_FORMAT_P010);
    assert(desc.width > 0 && desc.height > 0 && desc.generation > 0);
    mkvc_gpu_native_handle_desc native{};
    native.struct_size = sizeof(native);
    native.struct_version = 1;
    assert(mkvc_gpu_frame_get_native_handle(frame, &native) == MKVC_OK);
    assert(native.type == MKVC_GPU_NATIVE_VA_SURFACE);
    assert(native.borrowed != 0 && native.handles[0] != 0);

    assert(mkvc_decoder_close(decoder) == MKVC_OK);
    mkvc_decoder_destroy(decoder);
    decoder = nullptr;
    // The exported frame owns the oneVPL session after Capture destruction.
    assert(mkvc_gpu_frame_get_desc(frame, &desc) == MKVC_OK);
    assert(mkvc_gpu_frame_get_native_handle(frame, &native) == MKVC_OK);
    mkvc_gpu_frame_release(frame);
    return 0;
}
