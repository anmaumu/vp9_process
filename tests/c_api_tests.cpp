#include "mkvcodec/mkvc.h"

#include <cassert>
#include <cstring>
#include <vector>

int main() {
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
    assert(count <= 4);
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
            assert(capabilities[index].can_decode != 0 ||
                   capabilities[index].can_encode != 0);
            assert(capabilities[index].is_hardware == 1);
        }
    }
    assert(mkvc_get_backend_capabilities(nullptr, nullptr) ==
           MKVC_ERROR_INVALID_ARGUMENT);

    mkvc_pipeline_metrics metrics{};
    metrics.struct_size = sizeof(metrics);
    metrics.struct_version = 1;
    assert(mkvc_encoder_get_metrics(nullptr, &metrics) ==
           MKVC_ERROR_INVALID_ARGUMENT);
    assert(mkvc_decoder_get_metrics(nullptr, &metrics) ==
           MKVC_ERROR_INVALID_ARGUMENT);
    metrics.struct_size = sizeof(metrics) - 1;
    assert(mkvc_encoder_get_metrics(nullptr, &metrics) ==
           MKVC_ERROR_INVALID_ARGUMENT);

    assert(std::strcmp(mkvc_result_string(MKVC_OK), "ok") == 0);
    assert(std::strcmp(mkvc_result_string(MKVC_WOULD_BLOCK), "would block") == 0);
    assert(std::strcmp(mkvc_result_string(static_cast<mkvc_result>(999)),
                       "unknown result") == 0);
    return 0;
}
