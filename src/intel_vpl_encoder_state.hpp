#pragma once

#include "intel_vpl_encoder.hpp"

#if defined(MKVC_HAS_INTEL_ONEVPL)
#include <vpl/mfxvideo.h>

#include "gpu/intel/vpl_encoder_queue.hpp"
#include "gpu/intel/vpl_encoder_runtime.hpp"
#include "gpu/intel/vpl_imported_surface_tracker.hpp"
#endif

#include <cstdint>
#include <memory>

namespace mkvc {

/** Internal state shared by the oneVPL facade and GPU submission adapter. */
struct IntelVplEncoder::Impl {
#if defined(MKVC_HAS_INTEL_ONEVPL)
    mfxSession session = nullptr;
    std::unique_ptr<gpu::intel::VplEncoderRuntime> runtime;
    std::unique_ptr<gpu::intel::VplEncoderQueue> queue;
    std::unique_ptr<gpu::intel::VplImportedSurfaceTracker> imported_surfaces;
#endif
    uint32_t codec = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t fps_num = 0;
    uint32_t fps_den = 0;
    int64_t next_pts = 0;
    bool drained = false;
    bool closed = false;
};

}  // namespace mkvc
