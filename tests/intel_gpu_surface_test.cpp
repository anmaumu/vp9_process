#include "mkvcodec/mkvc.h"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <iostream>

namespace {
void release_source(void* opaque) {
    mkvc_gpu_frame_release(static_cast<mkvc_gpu_frame*>(opaque));
}
}

int main(int argc, char** argv) {
    if ((argc != 2 && argc != 3) || !std::filesystem::exists(argv[1])) return 77;
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

    mkvc_gpu_frame* imported = nullptr;
    if (argc == 3) {
        mkvc_gpu_external_frame_config external{};
        external.struct_size = sizeof(external);
        external.struct_version = 1;
        external.frame = desc;
        external.native_handle = native;
        external.release = release_source;
        external.user_data = frame;
        assert(mkvc_gpu_frame_retain(frame) == MKVC_OK);
        const mkvc_result imported_result =
            mkvc_gpu_frame_import_external(&external, &imported);
        if (imported_result != MKVC_OK) {
            std::cerr << mkvc_get_last_error() << '\n';
            mkvc_gpu_frame_release(frame);
            return 3;
        }
    }

    assert(mkvc_decoder_close(decoder) == MKVC_OK);
    mkvc_decoder_destroy(decoder);
    decoder = nullptr;
    // The exported frame owns the oneVPL session after Capture destruction.
    assert(mkvc_gpu_frame_get_desc(frame, &desc) == MKVC_OK);
    assert(mkvc_gpu_frame_get_native_handle(frame, &native) == MKVC_OK);
    if (imported != nullptr) {
        mkvc_encoder_config encoder_config{};
        encoder_config.struct_size = sizeof(encoder_config);
        encoder_config.struct_version = 1;
        encoder_config.output_path_utf8 = argv[2];
        encoder_config.codec = MKVC_CODEC_VP9;
        encoder_config.backend = MKVC_BACKEND_INTEL;
        encoder_config.width = desc.width;
        encoder_config.height = desc.height;
        encoder_config.fps_num = 30;
        encoder_config.fps_den = 1;
        encoder_config.quality = 32;
        mkvc_encoder* encoder = nullptr;
        if (mkvc_encoder_create(&encoder_config, &encoder) != MKVC_OK ||
            mkvc_encoder_write_gpu_frame(encoder, imported) != MKVC_OK ||
            mkvc_encoder_close(encoder) != MKVC_OK) {
            std::cerr << mkvc_get_last_error() << '\n';
            mkvc_encoder_destroy(encoder);
            return 4;
        }
        mkvc_encoder_destroy(encoder);
        mkvc_gpu_frame_release(imported);
        if (!std::filesystem::exists(argv[2]) ||
            std::filesystem::file_size(argv[2]) == 0) return 5;
    }
    mkvc_gpu_frame_release(frame);
    return 0;
}
