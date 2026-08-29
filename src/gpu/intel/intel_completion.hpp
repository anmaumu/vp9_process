#pragma once

#include "../gpu_frame.hpp"

#include <vpl/mfxvideo.h>

#include <memory>

namespace mkvc::gpu::intel {

/** oneVPL SyncPoint completion; session_keepalive owns the session lifetime. */
std::shared_ptr<Completion> make_sync_point_completion(
    mfxSession session, mfxSyncPoint sync_point,
    std::shared_ptr<void> session_keepalive = {});

}  // namespace mkvc::gpu::intel
