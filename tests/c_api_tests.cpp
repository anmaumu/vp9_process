#include "mkvcodec/mkvc.h"

#include <cassert>
#include <cstring>

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
    assert(count <= 1);
    if (count == 1) {
        mkvc_backend_capability capability{};
        size_t capacity = 1;
        assert(mkvc_get_backend_capabilities(&capability, &capacity) == MKVC_OK);
        assert(capacity == 1);
        assert(capability.struct_size == sizeof(capability));
        assert(capability.backend == MKVC_BACKEND_CPU);
        assert(capability.codec == MKVC_CODEC_VP9);
        assert(capability.can_decode == 0 && capability.can_encode == 1);
        assert(capability.is_hardware == 0);
    }
    assert(mkvc_get_backend_capabilities(nullptr, nullptr) ==
           MKVC_ERROR_INVALID_ARGUMENT);

    assert(std::strcmp(mkvc_result_string(MKVC_OK), "ok") == 0);
    assert(std::strcmp(mkvc_result_string(static_cast<mkvc_result>(999)),
                       "unknown result") == 0);
    return 0;
}
