#pragma once

#include "nvidia_webm_encoder.hpp"

#if defined(MKVC_HAS_NVIDIA)
#include "gpu/nvidia/nvenc_api.hpp"
#include "gpu/nvidia/nvenc_session.hpp"
#include "webm_muxer.hpp"
#endif

#include <cstdint>
#include <memory>
#include <vector>

namespace mkvc {

/** Mutable state shared by the NVIDIA encoder lifecycle and submission modules. */
struct NvidiaWebmEncoder::Impl {
#if defined(MKVC_HAS_NVIDIA)
    std::unique_ptr<gpu::nvidia::NvencApi> api;
    std::unique_ptr<gpu::nvidia::NvencSessionManager> session_manager;
    std::unique_ptr<WebmMuxer> muxer;
#endif
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t fps_num = 0;
    uint32_t fps_den = 0;
    uint32_t keyframe_interval = 0;
    uint64_t frame_index = 0;
    int64_t next_pts = 0;
    bool closed = false;
    std::vector<uint8_t> i420;
    std::vector<uint8_t> nv12;
};

}  // namespace mkvc
