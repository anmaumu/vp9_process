#include "gpu/intel/intel_import_contract.hpp"
#include <initializer_list>

namespace {
struct RefState { mfxU32 count = 2; mfxStatus status = MFX_ERR_NONE; };
mfxStatus MFX_CDECL fake_refcount(mfxFrameSurface1* surface, mfxU32* count) {
    const auto* state = static_cast<RefState*>(surface->FrameInterface->Context);
    *count = state->count;
    return state->status;
}
mfxStatus MFX_CDECL fake_import(mfxMemoryInterface*, mfxSurfaceComponent,
                              mfxSurfaceHeader*, mfxFrameSurface1**) {
    return MFX_ERR_UNSUPPORTED;
}
}

int main() {
    using mkvc::gpu::intel::can_release_imported_surface;
    RefState state;
    mfxFrameSurfaceInterface interface{};
    interface.Context = &state;
    interface.GetRefCounter = fake_refcount;
    mfxFrameSurface1 surface{};
    if (can_release_imported_surface(nullptr) || can_release_imported_surface(&surface)) return 5;
    surface.FrameInterface = &interface;
    if (can_release_imported_surface(&surface)) return 6;
    state.count = 1;
    surface.Data.Locked = 1;
    if (can_release_imported_surface(&surface)) return 7;
    surface.Data.Locked = 0;
    if (!can_release_imported_surface(&surface)) return 8;
    state.status = MFX_ERR_DEVICE_LOST;
    if (can_release_imported_surface(&surface)) return 9;
    interface.GetRefCounter = nullptr;
    if (can_release_imported_surface(&surface)) return 10;
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
