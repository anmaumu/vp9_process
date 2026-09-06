/**
 * @file vpl_decoder_runtime.hpp
 * @brief oneVPL hardware decoder session selection and lifetime ownership.
 */
#pragma once

#include <vpl/mfxvideo.h>

#include <cstdint>
#include <memory>
#include <string>

namespace mkvc::gpu::intel {

class VplSession;

/**
 * @brief Own the dispatcher and hardware session used by one decoder.
 *
 * The codec component is initialized lazily by VplDecoderPump because oneVPL
 * requires compressed header data first. A shared session lease is exposed to
 * the output queue so exported GPU surfaces can outlive IntelVplDecoder.
 */
class VplDecoderRuntime final {
   public:
    /**
     * @brief Select a hardware implementation supporting the requested codec.
     * @param codec MKVC_CODEC_VP9 or MKVC_CODEC_AV1.
     * @param error Receives a diagnostic on failure.
     * @return Selected runtime, or nullptr when unavailable.
     */
    static std::unique_ptr<VplDecoderRuntime> create(uint32_t codec, std::string& error);

    ~VplDecoderRuntime() = default;
    VplDecoderRuntime(const VplDecoderRuntime&) = delete;
    VplDecoderRuntime& operator=(const VplDecoderRuntime&) = delete;

    /** Borrow the selected oneVPL session. */
    mfxSession session() const noexcept;

    /** Share ownership with queues and exported GPU surface leases. */
    const std::shared_ptr<VplSession>& lifetime() const noexcept { return lifetime_; }

   private:
    VplDecoderRuntime() = default;
    std::shared_ptr<VplSession> lifetime_;
};

}  // namespace mkvc::gpu::intel
