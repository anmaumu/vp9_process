#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

#include "mkvcodec/mkvc.h"

int main() {
    static_assert(sizeof(mkvc_video_info) == 64);
    assert(MKVC_CODEC_AUTO == 0);
    mkvc_version version{};
    version.struct_size = sizeof(version);
    assert(mkvc_get_version(&version) == MKVC_OK);
    assert(version.abi_version == MKVC_ABI_VERSION);
    assert(version.major == 0 && version.minor == 1 && version.patch == 0);

    assert(mkvc_get_version(nullptr) == MKVC_ERROR_INVALID_ARGUMENT);

    mkvc_version undersized{};
    undersized.struct_size = sizeof(undersized) - 1;
    assert(mkvc_get_version(&undersized) == MKVC_ERROR_INVALID_ARGUMENT);

    size_t count = 123;
    assert(mkvc_get_backend_capabilities(nullptr, &count) == MKVC_OK);
    assert(count <= 6);
    if (count > 0) {
        std::vector<mkvc_backend_capability> capabilities(count);
        size_t capacity = capabilities.size();
        assert(mkvc_get_backend_capabilities(capabilities.data(), &capacity) == MKVC_OK);
        assert(capacity == count);
        for (size_t index = 0; index < count; ++index) {
            assert(capabilities[index].struct_size == sizeof(capabilities[index]));
            assert(capabilities[index].backend == MKVC_BACKEND_CPU ||
                   capabilities[index].backend == MKVC_BACKEND_INTEL ||
                   capabilities[index].backend == MKVC_BACKEND_NVIDIA);
        }
        assert(capabilities[0].codec == MKVC_CODEC_VP9);
        assert(capabilities[0].can_decode == 1 && capabilities[0].can_encode == 1);
        if (count == 2) {
            assert(capabilities[1].codec == MKVC_CODEC_AV1);
            assert(capabilities[1].can_decode == 1 && capabilities[1].can_encode == 1);
        }
        for (size_t index = 2; index < count; ++index) {
            assert(capabilities[index].backend == MKVC_BACKEND_INTEL ||
                   capabilities[index].backend == MKVC_BACKEND_NVIDIA);
            assert(capabilities[index].can_decode <= 1);
            assert(capabilities[index].can_encode <= 1);
            assert(capabilities[index].can_decode != 0 || capabilities[index].can_encode != 0);
            assert(capabilities[index].is_hardware == 1);
        }
    }
    assert(mkvc_get_backend_capabilities(nullptr, nullptr) == MKVC_ERROR_INVALID_ARGUMENT);

    mkvc_encoder_config unsupported_nvidia_vp9{};
    unsupported_nvidia_vp9.struct_size = sizeof(unsupported_nvidia_vp9);
    unsupported_nvidia_vp9.struct_version = 1;
    unsupported_nvidia_vp9.output_path_utf8 = "unsupported-nvidia-vp9.webm";
    unsupported_nvidia_vp9.codec = MKVC_CODEC_VP9;
    unsupported_nvidia_vp9.backend = MKVC_BACKEND_NVIDIA;
    unsupported_nvidia_vp9.width = 160;
    unsupported_nvidia_vp9.height = 128;
    unsupported_nvidia_vp9.fps_num = 30;
    unsupported_nvidia_vp9.fps_den = 1;
    unsupported_nvidia_vp9.quality = 32;
    auto expect_invalid_encoder_config = [](mkvc_encoder_config invalid) {
        auto* output = reinterpret_cast<mkvc_encoder*>(static_cast<uintptr_t>(1));
        assert(mkvc_encoder_create(&invalid, &output) == MKVC_ERROR_INVALID_ARGUMENT);
        assert(output == nullptr);
    };
    mkvc_encoder* invalid_output = reinterpret_cast<mkvc_encoder*>(static_cast<uintptr_t>(1));
    assert(mkvc_encoder_create(nullptr, &invalid_output) == MKVC_ERROR_INVALID_ARGUMENT);
    assert(invalid_output == nullptr);
    assert(mkvc_encoder_create(&unsupported_nvidia_vp9, nullptr) == MKVC_ERROR_INVALID_ARGUMENT);
    auto invalid_encoder_config = unsupported_nvidia_vp9;
    invalid_encoder_config.struct_size = sizeof(invalid_encoder_config) - 1;
    expect_invalid_encoder_config(invalid_encoder_config);
    invalid_encoder_config = unsupported_nvidia_vp9;
    invalid_encoder_config.struct_version = 0;
    expect_invalid_encoder_config(invalid_encoder_config);
    invalid_encoder_config = unsupported_nvidia_vp9;
    invalid_encoder_config.output_path_utf8 = "";
    expect_invalid_encoder_config(invalid_encoder_config);
    invalid_encoder_config = unsupported_nvidia_vp9;
    invalid_encoder_config.codec = 999;
    expect_invalid_encoder_config(invalid_encoder_config);
    invalid_encoder_config = unsupported_nvidia_vp9;
    invalid_encoder_config.backend = 999;
    expect_invalid_encoder_config(invalid_encoder_config);
    invalid_encoder_config = unsupported_nvidia_vp9;
    invalid_encoder_config.width = 159;
    expect_invalid_encoder_config(invalid_encoder_config);
    invalid_encoder_config = unsupported_nvidia_vp9;
    invalid_encoder_config.height = 127;
    expect_invalid_encoder_config(invalid_encoder_config);
    invalid_encoder_config = unsupported_nvidia_vp9;
    invalid_encoder_config.fps_num = 0;
    expect_invalid_encoder_config(invalid_encoder_config);
    invalid_encoder_config = unsupported_nvidia_vp9;
    invalid_encoder_config.quality = 64;
    expect_invalid_encoder_config(invalid_encoder_config);
    invalid_encoder_config = unsupported_nvidia_vp9;
    invalid_encoder_config.width = 536870912u;
    expect_invalid_encoder_config(invalid_encoder_config);
    mkvc_encoder* unsupported_encoder = nullptr;
    assert(mkvc_encoder_create(&unsupported_nvidia_vp9, &unsupported_encoder) ==
           MKVC_ERROR_NOT_SUPPORTED);
    assert(unsupported_encoder == nullptr);
    assert(std::strstr(mkvc_get_last_error(), "unavailable") != nullptr);

    mkvc_decoder_config decoder_config{};
    decoder_config.struct_size = sizeof(decoder_config);
    decoder_config.struct_version = 1;
    decoder_config.input_path_utf8 = "missing-input.webm";
    decoder_config.codec = MKVC_CODEC_VP9;
    decoder_config.backend = MKVC_BACKEND_CPU;
    auto expect_invalid_decoder_config = [](mkvc_decoder_config invalid) {
        auto* output = reinterpret_cast<mkvc_decoder*>(static_cast<uintptr_t>(1));
        assert(mkvc_decoder_create(&invalid, &output) == MKVC_ERROR_INVALID_ARGUMENT);
        assert(output == nullptr);
    };
    mkvc_decoder* invalid_decoder_output =
        reinterpret_cast<mkvc_decoder*>(static_cast<uintptr_t>(1));
    assert(mkvc_decoder_create(nullptr, &invalid_decoder_output) == MKVC_ERROR_INVALID_ARGUMENT);
    assert(invalid_decoder_output == nullptr);
    assert(mkvc_decoder_create(&decoder_config, nullptr) == MKVC_ERROR_INVALID_ARGUMENT);
    auto invalid_decoder_config = decoder_config;
    invalid_decoder_config.struct_size = sizeof(invalid_decoder_config) - 1;
    expect_invalid_decoder_config(invalid_decoder_config);
    invalid_decoder_config = decoder_config;
    invalid_decoder_config.struct_version = 0;
    expect_invalid_decoder_config(invalid_decoder_config);
    invalid_decoder_config = decoder_config;
    invalid_decoder_config.input_path_utf8 = "";
    expect_invalid_decoder_config(invalid_decoder_config);
    invalid_decoder_config = decoder_config;
    invalid_decoder_config.codec = 999;
    expect_invalid_decoder_config(invalid_decoder_config);
    invalid_decoder_config = decoder_config;
    invalid_decoder_config.backend = 999;
    expect_invalid_decoder_config(invalid_decoder_config);

    mkvc_video_info video_info{};
    video_info.struct_size = sizeof(video_info);
    video_info.struct_version = 1;
    assert(mkvc_probe_input(nullptr, &video_info) == MKVC_ERROR_INVALID_ARGUMENT);
    assert(mkvc_probe_input("missing-input.webm", nullptr) == MKVC_ERROR_INVALID_ARGUMENT);
    assert(mkvc_decoder_get_info(nullptr, &video_info) == MKVC_ERROR_INVALID_ARGUMENT);
    video_info.struct_size -= 1;
    assert(mkvc_probe_input("missing-input.webm", &video_info) == MKVC_ERROR_INVALID_ARGUMENT);

    mkvc_pipeline_metrics metrics{};
    metrics.struct_size = sizeof(metrics);
    metrics.struct_version = 1;
    assert(mkvc_encoder_get_metrics(nullptr, &metrics) == MKVC_ERROR_INVALID_ARGUMENT);
    assert(mkvc_decoder_get_metrics(nullptr, &metrics) == MKVC_ERROR_INVALID_ARGUMENT);
    metrics.struct_size = sizeof(metrics) - 1;
    assert(mkvc_encoder_get_metrics(nullptr, &metrics) == MKVC_ERROR_INVALID_ARGUMENT);

    assert(std::strcmp(mkvc_result_string(MKVC_OK), "ok") == 0);
    assert(std::strcmp(mkvc_result_string(MKVC_WOULD_BLOCK), "would block") == 0);
    assert(std::strcmp(mkvc_result_string(MKVC_ERROR_TIMEOUT), "timeout") == 0);
    assert(std::strcmp(mkvc_result_string(static_cast<mkvc_result>(999)), "unknown result") == 0);
    return 0;
}
