#include "mkvcodec/mkvc.h"

#include <cstdio>
#include <iostream>

namespace {
bool ok(mkvc_result result, const char* operation) {
    if (result == MKVC_OK) return true;
    std::cerr << operation << " failed: " << mkvc_get_last_error() << '\n';
    return false;
}
}

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    std::remove(argv[2]);
    mkvc_decoder_config decoder_config{};
    decoder_config.struct_size = sizeof(decoder_config);
    decoder_config.struct_version = 1;
    decoder_config.input_path_utf8 = argv[1];
    decoder_config.codec = MKVC_CODEC_VP9;
    decoder_config.backend = MKVC_BACKEND_NVIDIA;
    mkvc_decoder* decoder = nullptr;
    if (mkvc_decoder_create(&decoder_config, &decoder) != MKVC_OK) return 77;

    mkvc_copy_policy strict{};
    strict.struct_size = sizeof(strict);
    strict.struct_version = 1;
    strict.require_gpu_resident = 1;
    strict.allow_gpu_copy = 1;
    strict.allow_cpu_copy = 0;
    if (!ok(mkvc_decoder_set_copy_policy(decoder, &strict), "decoder policy"))
        return 3;
    mkvc_gpu_frame* surface = nullptr;
    if (!ok(mkvc_decoder_read_gpu(decoder, &surface), "first GPU read")) return 4;
    mkvc_gpu_frame_desc desc{};
    desc.struct_size = sizeof(desc);
    desc.struct_version = 1;
    if (!ok(mkvc_gpu_frame_get_desc(surface, &desc), "GPU descriptor")) return 5;

    mkvc_encoder_config encoder_config{};
    encoder_config.struct_size = sizeof(encoder_config);
    encoder_config.struct_version = 1;
    encoder_config.output_path_utf8 = argv[2];
    encoder_config.codec = MKVC_CODEC_AV1;
    encoder_config.backend = MKVC_BACKEND_NVIDIA;
    encoder_config.width = desc.width;
    encoder_config.height = desc.height;
    encoder_config.fps_num = 30;
    encoder_config.fps_den = 1;
    encoder_config.quality = 32;
    encoder_config.queue_size = 0;
    mkvc_encoder* encoder = nullptr;
    const mkvc_result created = mkvc_encoder_create(&encoder_config, &encoder);
    if (created == MKVC_ERROR_NOT_SUPPORTED) {
        mkvc_gpu_frame_release(surface);
        mkvc_decoder_destroy(decoder);
        return 77;
    }
    if (!ok(created, "NVENC create") ||
        !ok(mkvc_encoder_set_copy_policy(encoder, &strict), "encoder policy"))
        return 6;

    uint64_t count = 0;
    while (surface != nullptr) {
        if (!ok(mkvc_encoder_write_gpu_frame(encoder, surface), "GPU write"))
            return 7;
        mkvc_gpu_frame_release(surface);
        surface = nullptr;
        ++count;
        const mkvc_result read = mkvc_decoder_read_gpu(decoder, &surface);
        if (read == MKVC_END_OF_STREAM) break;
        if (!ok(read, "GPU read")) return 8;
    }
    if (!ok(mkvc_decoder_close(decoder), "decoder close")) return 9;
    mkvc_decoder_destroy(decoder);
    if (!ok(mkvc_encoder_close(encoder), "encoder close")) return 10;
    mkvc_pipeline_metrics metrics{};
    metrics.struct_size = sizeof(metrics);
    metrics.struct_version = 1;
    const bool valid = ok(mkvc_encoder_get_metrics(encoder, &metrics), "metrics") &&
        count != 0 && metrics.accepted_frames == count &&
        metrics.completed_frames == count &&
        metrics.copy_path == MKVC_COPY_PATH_ZERO_COPY;
    mkvc_encoder_destroy(encoder);
    return valid ? 0 : 11;
}
