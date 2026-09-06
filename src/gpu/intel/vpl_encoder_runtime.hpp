/**
 * @file vpl_encoder_runtime.hpp
 * @brief oneVPL encoder session creation and external-device lifetime.
 */
#pragma once

#include <vpl/mfxvideo.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "mkvcodec/mkvc.h"

namespace mkvc::gpu {

class GpuFrameCore;

namespace intel {

/** Configuration required to create a hardware oneVPL encode component. */
struct VplEncoderRuntimeConfig {
    uint32_t codec = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t fps_num = 0;
    uint32_t fps_den = 0;
    uint32_t quality = 0;
    uint32_t keyframe_interval_frames = 0;
    uint32_t async_depth = 0;
};

/**
 * @brief Own an initialized oneVPL encode session and its device lease.
 *
 * Creation applies hardware/codec/device filters, initializes NV12 encode
 * parameters, verifies the selected I/O pattern, and sizes the bitstream buffer.
 * When an external device is selected, one opaque frame lease keeps that device
 * alive until the encode component and session have been destroyed.
 */
class VplEncoderRuntime final {
   public:
    /**
     * @brief Create and initialize the selected Intel hardware encoder.
     * @param config Validated public encoder configuration.
     * @param external_device_owner Optional frame identifying the shared device.
     * @param bitstream_capacity Receives the minimum safe queue buffer capacity.
     * @param error Receives a diagnostic on failure.
     * @return Initialized runtime, or nullptr on failure.
     */
    static std::unique_ptr<VplEncoderRuntime> create(
        const VplEncoderRuntimeConfig& config,
        const std::shared_ptr<GpuFrameCore>& external_device_owner, size_t& bitstream_capacity,
        std::string& error);

    ~VplEncoderRuntime();
    VplEncoderRuntime(const VplEncoderRuntime&) = delete;
    VplEncoderRuntime& operator=(const VplEncoderRuntime&) = delete;

    /** Borrow the initialized oneVPL session. */
    mfxSession session() const noexcept;

    /** Return whether the runtime is fixed to an externally owned GPU device. */
    bool uses_external_device() const noexcept { return external_device_owner_ != nullptr; }

    /** Return the D3D11-device or VA-display identity selected at creation. */
    uint64_t external_device_identity() const noexcept { return external_device_identity_; }

   private:
    class State;
    VplEncoderRuntime();

    std::unique_ptr<State> state_;
    ::mkvc_gpu_frame* external_device_owner_ = nullptr;
    uint64_t external_device_identity_ = 0;
};

}  // namespace intel
}  // namespace mkvc::gpu
