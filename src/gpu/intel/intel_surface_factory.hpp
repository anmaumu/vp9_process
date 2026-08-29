#pragma once

#include "../gpu_frame_pool.hpp"

#include <vpl/mfxvideo.h>

#include <memory>
#include <string>

namespace mkvc::gpu::intel {

/** Wrap one oneVPL video-memory decode/VPP surface without mapping it to CPU. */
mkvc_result wrap_vpl_surface(
    mfxSession session, mfxFrameSurface1* surface, mfxSyncPoint sync_point,
    uint64_t device_id, const std::shared_ptr<void>& session_keepalive,
    const std::shared_ptr<GpuFramePool>& pool,
    GpuFramePool::Acquisition& output, std::string& error);

}  // namespace mkvc::gpu::intel
