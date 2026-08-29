#include "backend_registry.hpp"
#include "intel_vpl_probe.hpp"

namespace mkvc {

const std::vector<mkvc_backend_capability>& backend_capabilities() {
    static const std::vector<mkvc_backend_capability> capabilities = [] {
        std::vector<mkvc_backend_capability> result = {
#if defined(MKVC_HAS_CPU_VP9)
        {sizeof(mkvc_backend_capability), MKVC_BACKEND_CPU, MKVC_CODEC_VP9,
         1, 1, 0, 0},
#endif
#if defined(MKVC_HAS_CPU_AV1)
        {sizeof(mkvc_backend_capability), MKVC_BACKEND_CPU, MKVC_CODEC_AV1,
         1, 1, 0, 0},
#endif
        };
#if defined(MKVC_HAS_INTEL_ONEVPL)
        const IntelVplProbeResult intel = probe_intel_vpl();
        if (intel.runtime_available && intel.vp9_encode) {
            result.push_back({sizeof(mkvc_backend_capability),
                              MKVC_BACKEND_INTEL, MKVC_CODEC_VP9,
                              0, 1, 1, 0});
        }
        if (intel.runtime_available && intel.av1_encode) {
            result.push_back({sizeof(mkvc_backend_capability),
                              MKVC_BACKEND_INTEL, MKVC_CODEC_AV1,
                              0, 1, 1, 0});
        }
#endif
        return result;
    }();
    return capabilities;
}

}  // namespace mkvc
