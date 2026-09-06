/**
 * @file vpl_decoder_pump.hpp
 * @brief Shared oneVPL compressed-input and lazy decoder initialization path.
 */
#pragma once

#include <vpl/mfxvideo.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "mkvcodec/mkvc.h"

namespace mkvc {
struct DecodedFrame;
namespace gpu {
class GpuFrameCore;
namespace intel {
class VplDecoderQueue;
class VplSession;

/**
 * @brief Feed owned compressed packets through one lazy-initialized decoder.
 *
 * Input bytes are copied because DecodeFrameAsync may retain access after the
 * caller returns. CPU and GPU collection share identical queue-pressure and
 * progress handling; only the output collector differs.
 */
class VplDecoderPump final {
   public:
    VplDecoderPump(uint32_t codec, bool gpu_output, mfxSession session,
                   std::shared_ptr<VplSession> lifetime, VplDecoderQueue& queue);
    ~VplDecoderPump() = default;
    VplDecoderPump(const VplDecoderPump&) = delete;
    VplDecoderPump& operator=(const VplDecoderPump&) = delete;

    /** Submit a compressed packet and collect completed CPU frames. */
    mkvc_result decode_cpu(const uint8_t* data, size_t size, int64_t pts,
                           std::vector<std::unique_ptr<DecodedFrame>>& frames, std::string& error);

    /** Submit a compressed packet and collect completed GPU surfaces. */
    mkvc_result decode_gpu(const uint8_t* data, size_t size, int64_t pts,
                           std::vector<std::shared_ptr<GpuFrameCore>>& frames, std::string& error);

    /** Return whether DecodeHeader and decoder initialization succeeded. */
    bool initialized() const noexcept { return initialized_; }

   private:
    mkvc_result prepare(const uint8_t* data, size_t size, int64_t pts, std::string& error);
    mkvc_result initialize(std::string& error);

    uint32_t codec_;
    bool gpu_output_;
    mfxSession session_;
    std::shared_ptr<VplSession> lifetime_;
    VplDecoderQueue& queue_;
    mfxVideoParam parameters_{};
    mfxBitstream bitstream_{};
    std::vector<uint8_t> packet_;
    bool initialized_ = false;
};

}  // namespace intel
}  // namespace gpu
}  // namespace mkvc
