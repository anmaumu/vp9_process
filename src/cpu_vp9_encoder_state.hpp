#pragma once

#include "cpu_vp9_encoder.hpp"
#include "encoder/frame_timing.hpp"
#include "webm_muxer.hpp"

#if defined(MKVC_HAS_CPU_VP9)
#include <vpx/vpx_encoder.h>
#endif

#include <cstdint>
#include <memory>
#include <vector>

namespace mkvc {

/** Internal mutable state shared by the CPU VP9 lifecycle and libvpx runtime. */
struct CpuVp9Encoder::Impl {
#if defined(MKVC_HAS_CPU_VP9)
    vpx_codec_ctx_t codec{};
    bool codec_initialized = false;
    std::unique_ptr<WebmMuxer> muxer;
#endif
    uint32_t width = 0;
    uint32_t height = 0;
    encoder::FrameTiming timing;
    bool closed = false;
    std::vector<uint8_t> image;
};

}  // namespace mkvc
