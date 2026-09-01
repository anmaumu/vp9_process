#include "mkvcodec/mkvc.h"

#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>

namespace {
void release_source(void* opaque) {
    mkvc_gpu_frame_release(static_cast<mkvc_gpu_frame*>(opaque));
}

int unavailable(const char* message) {
    std::cerr << message << '\n';
    return std::getenv("MKVC_REQUIRE_INTEL_EXTERNAL_IMPORT") != nullptr ? 1 : 77;
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
    if (frame == nullptr || mkvc_gpu_frame_wait(frame, 5000) != MKVC_OK) {
        std::cerr << mkvc_get_last_error() << '\n';
        mkvc_decoder_destroy(decoder);
        return 2;
    }
    mkvc_gpu_frame_desc desc{};
    desc.struct_size = sizeof(desc);
    desc.struct_version = 1;
    if (mkvc_gpu_frame_get_desc(frame, &desc) != MKVC_OK) {
        std::cerr << mkvc_get_last_error() << '\n';
        mkvc_gpu_frame_release(frame);
        mkvc_decoder_destroy(decoder);
        return 2;
    }
    if (desc.backend != MKVC_BACKEND_INTEL ||
        desc.memory_type != MKVC_GPU_MEMORY_VA_SURFACE ||
        (desc.pixel_format != MKVC_PIXEL_FORMAT_NV12 &&
         desc.pixel_format != MKVC_PIXEL_FORMAT_P010) ||
        desc.width == 0 || desc.height == 0 || desc.generation == 0) {
        std::cerr << "invalid Intel GPU frame descriptor\n";
        mkvc_gpu_frame_release(frame);
        mkvc_decoder_destroy(decoder);
        return 2;
    }
    mkvc_gpu_native_handle_desc native{};
    native.struct_size = sizeof(native);
    native.struct_version = 1;
    if (mkvc_gpu_frame_get_native_handle(frame, &native) != MKVC_OK) {
        std::cerr << mkvc_get_last_error() << '\n';
        mkvc_gpu_frame_release(frame);
        mkvc_decoder_destroy(decoder);
        return 2;
    }
    if (native.type != MKVC_GPU_NATIVE_VA_SURFACE || native.borrowed == 0 ||
        native.handles[0] == 0) {
        std::cerr << "invalid Intel VA native handle\n";
        mkvc_gpu_frame_release(frame);
        mkvc_decoder_destroy(decoder);
        return 2;
    }

    mkvc_gpu_frame* imported = nullptr;
    if (argc == 3) {
        mkvc_gpu_external_frame_config external{};
        external.struct_size = sizeof(external);
        external.struct_version = 1;
        external.frame = desc;
        external.native_handle = native;
        external.release = release_source;
        external.user_data = frame;
        if (mkvc_gpu_frame_retain(frame) != MKVC_OK) {
            mkvc_gpu_frame_release(frame);
            mkvc_decoder_destroy(decoder);
            return 3;
        }
        const mkvc_result imported_result =
            mkvc_gpu_frame_import_external(&external, &imported);
        if (imported_result != MKVC_OK) {
            std::cerr << mkvc_get_last_error() << " desc=(" << desc.width
                      << 'x' << desc.height << ", planes=" << desc.plane_count
                      << ", dev=" << desc.device_id << ", gen="
                      << desc.generation << ") native=(dev=" << native.device_id
                      << ", gen=" << native.generation << ", borrowed="
                      << native.borrowed << ")\n";
            // Import did not accept release_source, so undo both the explicit
            // retain above and the original frame lease here.
            mkvc_gpu_frame_release(frame);
            mkvc_gpu_frame_release(frame);
            mkvc_decoder_destroy(decoder);
            return 3;
        }
    }

    if (mkvc_decoder_close(decoder) != MKVC_OK) {
        std::cerr << mkvc_get_last_error() << '\n';
        mkvc_gpu_frame_release(imported);
        mkvc_gpu_frame_release(frame);
        mkvc_decoder_destroy(decoder);
        return 4;
    }
    mkvc_decoder_destroy(decoder);
    decoder = nullptr;
    // The exported frame owns the oneVPL session after Capture destruction.
    if (mkvc_gpu_frame_get_desc(frame, &desc) != MKVC_OK ||
        mkvc_gpu_frame_get_native_handle(frame, &native) != MKVC_OK) {
        mkvc_gpu_frame_release(imported);
        mkvc_gpu_frame_release(frame);
        return 4;
    }
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
        const mkvc_result encoder_created =
            mkvc_encoder_create(&encoder_config, &encoder);
        if (encoder_created != MKVC_OK) {
            std::cerr << mkvc_get_last_error() << '\n';
            mkvc_encoder_destroy(encoder);
            mkvc_gpu_frame_release(imported);
            mkvc_gpu_frame_release(frame);
            return 4;
        }
        const mkvc_result written =
            mkvc_encoder_write_gpu_frame(encoder, imported);
        if (written == MKVC_ERROR_NOT_SUPPORTED) {
            const std::string message = mkvc_get_last_error();
            mkvc_encoder_destroy(encoder);
            mkvc_gpu_frame_release(imported);
            mkvc_gpu_frame_release(frame);
            return unavailable(message.c_str());
        }
        if (written != MKVC_OK || mkvc_encoder_close(encoder) != MKVC_OK) {
            std::cerr << mkvc_get_last_error() << '\n';
            mkvc_encoder_destroy(encoder);
            mkvc_gpu_frame_release(imported);
            mkvc_gpu_frame_release(frame);
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
