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

}  // namespace mkvc::gpu::intel
