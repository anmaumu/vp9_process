#pragma once

#include "cpu_vp9_decoder.hpp"
#include "mkvcodec/mkvc.h"
#include "gpu/gpu_frame.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace mkvc {

/** oneVPL 2.x hardware decoder returning library-owned I420 frames. */
class IntelVplDecoder {
 public:
    struct Impl;

    /** Create a hardware session filtered to the requested VP9 or AV1 decoder. */
    static std::unique_ptr<IntelVplDecoder> create(
        uint32_t codec, std::string& error, uint32_t async_depth = 4,
        bool gpu_output = false);
    ~IntelVplDecoder();

    IntelVplDecoder(const IntelVplDecoder&) = delete;
    IntelVplDecoder& operator=(const IntelVplDecoder&) = delete;

    /** Submit one complete compressed frame and return completed I420 frames. */
    mkvc_result decode(const uint8_t* data, size_t size, int64_t pts,
                       std::vector<std::unique_ptr<DecodedFrame>>& frames,
                       std::string& error);
    /** Drain delayed frames after the final compressed packet. */
    mkvc_result drain(std::vector<std::unique_ptr<DecodedFrame>>& frames,
                      std::string& error);
    /** Submit compressed data and return leased video-memory surfaces. */
    mkvc_result decode_gpu(
        const uint8_t* data, size_t size, int64_t pts,
        std::vector<std::shared_ptr<gpu::GpuFrameCore>>& frames,
        std::string& error);
    /** Drain delayed video-memory surfaces. */
    mkvc_result drain_gpu(
        std::vector<std::shared_ptr<gpu::GpuFrameCore>>& frames,
        std::string& error);
    /** Release decoder, session, and dispatcher resources idempotently. */
    mkvc_result close(std::string& error);
    /** Largest number of simultaneously outstanding decode SyncPoints. */
    uint32_t max_pending_observed() const;
#if defined(MKVC_ENABLE_TEST_HOOKS)
    /** Test-only hook: fail collection after N successful SyncPoints. */
    void set_test_device_loss_after(uint32_t completed_syncpoints);
#endif

 private:
    IntelVplDecoder();
    std::unique_ptr<Impl> impl_;
};

}  // namespace mkvc
