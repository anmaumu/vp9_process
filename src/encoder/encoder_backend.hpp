#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "mkvcodec/mkvc.h"

namespace mkvc {
namespace gpu {
class GpuFrameCore;
}

/** @brief Type-erased codec/container backend owned by one encoder session. */
class EncoderBackend {
   public:
    virtual ~EncoderBackend() = default;
    /** @brief Encode one CPU frame. */
    virtual mkvc_result write(const mkvc_frame_view& frame, std::string& error) = 0;
    /** @brief Drain all accepted input while keeping the backend usable. */
    virtual mkvc_result flush(std::string& error) = 0;
    /** @brief Finalize output and release backend resources. */
    virtual mkvc_result close(std::string& error) = 0;
    /** @brief Report whether write_gpu can consume GPU frame leases. */
    virtual bool supports_gpu_frames() const noexcept = 0;
    /** @brief Encode one compatible GPU-resident frame lease. */
    virtual mkvc_result write_gpu(const std::shared_ptr<gpu::GpuFrameCore>& frame,
                                  std::string& error) = 0;
    /** @brief Return the backend's observed in-flight hardware work. */
    virtual uint32_t hardware_pending() const noexcept = 0;
};

/**
 * @brief Adapt a concrete CPU or GPU encoder to the session backend contract.
 * @tparam Encoder Concrete codec/container encoder type.
 * @tparam SupportsGpu Whether the concrete type accepts GPU-frame leases.
 */
template <typename Encoder, bool SupportsGpu>
class EncoderBackendAdapter final : public EncoderBackend {
   public:
    /** @brief Take exclusive ownership of the concrete encoder. */
    explicit EncoderBackendAdapter(std::unique_ptr<Encoder> encoder)
        : encoder_(std::move(encoder)) {}

    mkvc_result write(const mkvc_frame_view& frame, std::string& error) override {
        return encoder_->write(frame, error);
    }

    mkvc_result flush(std::string& error) override { return encoder_->flush(error); }

    mkvc_result close(std::string& error) override { return encoder_->close(error); }

    bool supports_gpu_frames() const noexcept override { return SupportsGpu; }

    mkvc_result write_gpu(const std::shared_ptr<gpu::GpuFrameCore>& frame,
                          std::string& error) override {
        if constexpr (SupportsGpu) {
            return encoder_->write_gpu(frame, error);
        } else {
            (void)frame;
            error = "GPU frame is not compatible with this encoder backend";
            return MKVC_ERROR_NOT_SUPPORTED;
        }
    }

    uint32_t hardware_pending() const noexcept override {
        if constexpr (SupportsGpu) {
            return encoder_->max_pending_observed();
        } else {
            return 0;
        }
    }

   private:
    std::unique_ptr<Encoder> encoder_;
};

}  // namespace mkvc
