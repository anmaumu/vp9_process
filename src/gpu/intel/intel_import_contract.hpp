#pragma once

#include <vpl/mfxmemory.h>

namespace mkvc::gpu::intel {

/** ImportFrameSurface belongs to memory interface 1.0 (oneVPL API 2.10).
 * Accept additive minor revisions, but do not guess a future major ABI layout.
 * Interface availability is not a guarantee that a particular surface imports.
 */
inline bool has_surface_import(const mfxMemoryInterface* memory) noexcept {
    return memory != nullptr && memory->Version.Major == 1 &&
           memory->ImportFrameSurface != nullptr;
}

/** Our last wrapper reference may retire only when the runtime no longer uses it. */
inline bool can_release_imported_surface(mfxFrameSurface1* surface) noexcept {
    mfxU32 references = 0;
    return surface != nullptr && surface->FrameInterface != nullptr &&
           surface->FrameInterface->GetRefCounter != nullptr &&
           surface->FrameInterface->GetRefCounter(surface, &references) == MFX_ERR_NONE &&
           references == 1 && surface->Data.Locked == 0;
}

}  // namespace mkvc::gpu::intel
