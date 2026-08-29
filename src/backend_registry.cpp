#include "backend_registry.hpp"

namespace mkvc {

const std::vector<mkvc_backend_capability>& backend_capabilities() {
    static const std::vector<mkvc_backend_capability> capabilities = {
#if defined(MKVC_HAS_CPU_VP9)
        {sizeof(mkvc_backend_capability), MKVC_BACKEND_CPU, MKVC_CODEC_VP9,
         0, 1, 0, 0},
#endif
    };
    return capabilities;
}

}  // namespace mkvc
