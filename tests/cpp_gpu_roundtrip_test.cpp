#include "mkvcodec/mkvcodec.hpp"

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <vector>

namespace {

bool supports(uint32_t backend, uint32_t codec, bool encode) {
    size_t count = 0;
    if (mkvc_get_backend_capabilities(nullptr, &count) != MKVC_OK) return false;
    std::vector<mkvc_backend_capability> capabilities(count);
    if (count != 0 && mkvc_get_backend_capabilities(capabilities.data(), &count) != MKVC_OK)
        return false;
    for (const auto& item : capabilities) {
        if (item.backend == backend && item.codec == codec &&
            (encode ? item.can_encode : item.can_decode) != 0)
            return true;
    }
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    assert(argc == 5);
    const uint32_t backend = static_cast<uint32_t>(std::strtoul(argv[3], nullptr, 10));
    const uint32_t output_codec = static_cast<uint32_t>(std::strtoul(argv[4], nullptr, 10));
    if (!supports(backend, MKVC_CODEC_VP9, false) ||
        !supports(backend, output_codec, true))
        return 77;

    mkvc_video_info info{};
    info.struct_size = sizeof(info);
    info.struct_version = 1;
    mkvcodec::check(mkvc_probe_input(argv[1], &info));
    std::filesystem::remove(argv[2]);

    mkvc_decoder_config decoder_config{};
    decoder_config.struct_size = sizeof(decoder_config);
    decoder_config.struct_version = 1;
    decoder_config.input_path_utf8 = argv[1];
    decoder_config.codec = MKVC_CODEC_VP9;
    decoder_config.backend = backend;
    mkvcodec::Decoder decoder(decoder_config);

    mkvc_encoder_config encoder_config{};
    encoder_config.struct_size = sizeof(encoder_config);
    encoder_config.struct_version = 1;
    encoder_config.output_path_utf8 = argv[2];
    encoder_config.codec = output_codec;
    encoder_config.backend = backend;
    encoder_config.width = info.width;
    encoder_config.height = info.height;
    encoder_config.fps_num = info.fps_known ? info.fps_num : 30;
    encoder_config.fps_den = info.fps_known ? info.fps_den : 1;
    encoder_config.quality = 32;
    mkvcodec::Encoder encoder(encoder_config);

    mkvc_copy_policy strict{};
    strict.struct_size = sizeof(strict);
    strict.struct_version = 1;
    strict.require_gpu_resident = 1;
    strict.allow_gpu_copy = 1;
    decoder.set_copy_policy(strict);
    encoder.set_copy_policy(strict);

    uint64_t count = 0;
    while (auto frame = decoder.read_gpu()) {
        const auto descriptor = frame->descriptor();
        assert(descriptor.backend == backend);
        assert(descriptor.width == info.width && descriptor.height == info.height);
        encoder.write(*frame);
        ++count;
    }
    decoder.close();
    encoder.close();
    assert(count == info.frame_count);
    assert(decoder.metrics().copy_path == MKVC_COPY_PATH_ZERO_COPY);
    assert(encoder.metrics().copy_path == MKVC_COPY_PATH_ZERO_COPY);
    assert(std::filesystem::file_size(argv[2]) > 0);
    return 0;
}
