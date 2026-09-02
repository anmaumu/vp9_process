#pragma once

// Qualification-only device selection; absent from non-test builds and Windows.
// https://github.com/intel/libvpl/blob/main/doc/multi-adapter-guide.md
#if defined(MKVC_HAS_INTEL_ONEVPL) && defined(MKVC_ENABLE_TEST_HOOKS) && defined(__linux__)
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <vpl/mfxdispatcher.h>

namespace mkvc::gpu::intel {
inline bool test_render_node_filter(mfxLoader loader) {
    const char* text = std::getenv("MKVC_TEST_INTEL_DRM_RENDER_NODE");
    if (!text) return true;
    unsigned node = 0;
    const char* end = text + std::strlen(text);
    const auto parsed = std::from_chars(text, end, node);
    if (parsed.ec != std::errc{} || parsed.ptr != end || node < 128 || node > 255)
        return false;
    mfxConfig config = MFXCreateConfig(loader);
    mfxVariant value{};
    value.Type = MFX_VARIANT_TYPE_U32;
    value.Data.U32 = node;
    return config && MFXSetConfigFilterProperty(config,
        reinterpret_cast<const mfxU8*>("mfxExtendedDeviceId.DRMRenderNodeNum"), value) == MFX_ERR_NONE;
}
}
#endif
