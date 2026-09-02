#include "mkvcodec/mkvc.h"

#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>

namespace {
#if defined(_WIN32)
constexpr uint32_t kMemoryType = MKVC_GPU_MEMORY_D3D11_TEXTURE;
constexpr uint32_t kNativeType = MKVC_GPU_NATIVE_D3D11_TEXTURE;
#else
constexpr uint32_t kMemoryType = MKVC_GPU_MEMORY_VA_SURFACE;
constexpr uint32_t kNativeType = MKVC_GPU_NATIVE_VA_SURFACE;
#endif
int releases = 0;
int queries = 0;
void release_source(void* opaque) {
    ++releases;
    mkvc_gpu_frame_release(static_cast<mkvc_gpu_frame*>(opaque));
}
mkvc_result query_source(void*, uint32_t* complete) {
    *complete = ++queries >= 3 ? 1u : 0u;
    return MKVC_OK;
}

int unavailable(const char* message) {
    std::cerr << message << '\n';
    return std::getenv("MKVC_REQUIRE_INTEL_EXTERNAL_IMPORT") != nullptr ? 1 : 77;
}

bool verify_output(const char* path, uint32_t width, uint32_t height) {
    mkvc_decoder_config config{};
    config.struct_size = sizeof(config);
    config.struct_version = 1;
    config.input_path_utf8 = path;
    config.codec = MKVC_CODEC_VP9;
    config.backend = MKVC_BACKEND_CPU;
    mkvc_decoder* decoder = nullptr;
    if (mkvc_decoder_create(&config, &decoder) != MKVC_OK) return false;
    unsigned count = 0;
    int64_t previous_pts = -1;
    bool valid = true;
    while (valid) {
        mkvc_frame* frame = nullptr;
        const auto result = mkvc_decoder_read(decoder, &frame);
        if (result == MKVC_END_OF_STREAM) break;
        if (result != MKVC_OK) { valid = false; break; }
        mkvc_frame_view view{};
        view.struct_size = sizeof(view);
        view.struct_version = 1;
        valid = mkvc_frame_get_view(frame, &view) == MKVC_OK &&
            view.width == width && view.height == height && view.pts > previous_pts;
        if (valid) {
            // Fixture frame zero contains this gradient. All eight imports use
            // the same native surface; reject corruption or a wrong surface.
            double squared_error = 0;
            for (uint32_t y = 0; y < height; ++y)
                for (uint32_t x = 0; x < width; ++x) {
                    const double difference = view.planes[0][y * view.strides[0] + x] -
                        static_cast<uint8_t>((x * 3 + y * 2) & 0xff);
                    squared_error += difference * difference;
                }
            valid = squared_error / (static_cast<double>(width) * height) < 205.63;
        }
        previous_pts = view.pts;
        ++count;
        mkvc_frame_release(frame);
    }
    mkvc_decoder_destroy(decoder);
    return valid && count == 8;
}
}

int main(int argc, char** argv) {
    if ((argc != 2 && argc != 3 && argc != 4) || !std::filesystem::exists(argv[1]))
        return unavailable("Intel GPU input fixture is missing");
    const bool native_va_sync = argc == 4;
    mkvc_decoder_config config{};
    config.struct_size = sizeof(config);
    config.struct_version = 1;
    config.input_path_utf8 = argv[1];
    config.codec = MKVC_CODEC_VP9;
    config.backend = MKVC_BACKEND_INTEL;
    config.prefetch = 0;
    mkvc_decoder* decoder = nullptr;
    const mkvc_result created = mkvc_decoder_create(&config, &decoder);
    if (created == MKVC_ERROR_NOT_SUPPORTED) return unavailable(mkvc_get_last_error());
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
        mkvc_gpu_frame_release(frame);
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
        desc.memory_type != kMemoryType ||
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
    if (native.type != kNativeType || native.borrowed == 0 ||
        native.handles[0] == 0) {
        std::cerr << "invalid Intel VA native handle\n";
        mkvc_gpu_frame_release(frame);
        mkvc_decoder_destroy(decoder);
        return 2;
    }

    mkvc_gpu_frame* imported = nullptr;
    if (argc >= 3) {
        mkvc_gpu_external_frame_config external{};
        external.struct_size = sizeof(external);
        external.struct_version = 1;
        external.frame = desc;
        external.native_handle = native;
        external.release = release_source;
        external.query = native_va_sync ? nullptr : query_source;
        external.user_data = frame;
        if (mkvc_gpu_frame_retain(frame) != MKVC_OK) {
            mkvc_gpu_frame_release(frame);
            mkvc_decoder_destroy(decoder);
            return 3;
        }
        const mkvc_result imported_result = native_va_sync
            ? mkvc_gpu_frame_import_va_surface(&external, &imported)
            : mkvc_gpu_frame_import_external(&external, &imported);
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
            return imported_result == MKVC_ERROR_NOT_SUPPORTED
                ? unavailable("native VA synchronization unavailable") : 3;
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
        bool valid = written == MKVC_OK &&
            (native_va_sync || queries >= 3) && releases == 0;
#if !defined(_WIN32)
        // A mismatching display must be rejected before dereferencing or
        // passing the deliberately invalid test display to a vendor function.
        mkvc_gpu_external_frame_config wrong{};
        wrong.struct_size = sizeof(wrong);
        wrong.struct_version = 1;
        wrong.frame = desc;
        wrong.native_handle = native;
        wrong.native_handle.handles[0] ^= 1;
        wrong.release = [](void*) {};
        mkvc_gpu_frame* mismatched = nullptr;
        valid = valid && mkvc_gpu_frame_import_external(&wrong, &mismatched) == MKVC_OK;
        if (valid) valid = mkvc_encoder_write_gpu_frame(encoder, mismatched) ==
            MKVC_ERROR_INVALID_ARGUMENT;
        mkvc_gpu_frame_release(mismatched);
#endif
        for (unsigned index = 1; valid && index < 8; ++index) {
            if (index == 4) valid = mkvc_encoder_flush(encoder) == MKVC_OK;
            if (valid) valid = mkvc_encoder_write_gpu_frame(encoder, imported) == MKVC_OK;
        }
        mkvc_pipeline_metrics metrics{};
        metrics.struct_size = sizeof(metrics);
        metrics.struct_version = 1;
        valid = valid && mkvc_encoder_get_metrics(encoder, &metrics) == MKVC_OK &&
            metrics.accepted_frames == 8 && metrics.completed_frames == 8 &&
            metrics.copy_path == MKVC_COPY_PATH_ZERO_COPY;
        // The writer owns the first imported resource's device/display lease
        // through its sequence, even when both caller handles are dropped.
        mkvc_gpu_frame_release(imported);
        imported = nullptr;
        mkvc_gpu_frame_release(frame);
        frame = nullptr;
        valid = valid && releases == 0;
        if (!valid || mkvc_encoder_close(encoder) != MKVC_OK || releases != 1) {
            std::cerr << mkvc_get_last_error() << '\n';
            mkvc_encoder_destroy(encoder);
            mkvc_gpu_frame_release(imported);
            mkvc_gpu_frame_release(frame);
            return 4;
        }
        mkvc_encoder_destroy(encoder);
        if (!std::filesystem::exists(argv[2]) ||
            std::filesystem::file_size(argv[2]) == 0 ||
            !verify_output(argv[2], desc.width, desc.height)) return 5;
    }
    mkvc_gpu_frame_release(frame);
    return 0;
}
