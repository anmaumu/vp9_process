#include "gpu/intel/intel_import_contract.hpp"
#include <initializer_list>

namespace {
mfxStatus MFX_CDECL fake_import(mfxMemoryInterface*, mfxSurfaceComponent,
                              mfxSurfaceHeader*, mfxFrameSurface1**) {
    return MFX_ERR_UNSUPPORTED;
}
}

int main() {
    using mkvc::gpu::intel::has_surface_import;
    if (has_surface_import(nullptr)) return 1;
    mfxMemoryInterface memory{};
    memory.Version.Major = 1;
    if (has_surface_import(&memory)) return 2;
    memory.ImportFrameSurface = fake_import;
    // Crucial regression: importing does not require minor version 1.
    for (unsigned minor : {0u, 1u, 255u}) {
        memory.Version.Minor = static_cast<mfxU8>(minor);
        if (!has_surface_import(&memory)) return 3;
    }
    for (unsigned major : {0u, 2u}) {
        memory.Version.Major = static_cast<mfxU8>(major);
        if (has_surface_import(&memory)) return 4;
    }
    return 0;
}
