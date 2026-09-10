#pragma once

#include "cpu_av1_encoder.hpp"
#include "encoder/frame_timing.hpp"
#include "webm_muxer.hpp"

#if defined(MKVC_HAS_CPU_AV1)
#include <svt-av1/EbSvtAv1Enc.h>
#endif

#include <cstdint>
#include <memory>
#include <vector>

namespace mkvc {

/** Internal mutable state shared by the CPU AV1 lifecycle and SVT runtime. */
struct CpuAv1Encoder::Impl {
#if defined(MKVC_HAS_CPU_AV1)
    EbComponentType* codec = nullptr;
    bool codec_initialized = false;
    std::unique_ptr<WebmMuxer> muxer;
#endif
    uint32_t width = 0;
    uint32_t height = 0;
    encoder::FrameTiming timing;
    uint32_t quality = 32;
    uint32_t keyframe_interval_frames = 0;
    uint64_t frames_in_sequence = 0;
    bool eos_sent = false;
    bool closed = false;
    std::vector<uint8_t> image;
};

}  // namespace mkvc
