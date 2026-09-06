/**
 * @file vpl_encoder_sequence.hpp
 * @brief oneVPL encoder mode, timestamp, flush, and restart coordination.
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "mkvcodec/mkvc.h"

namespace mkvc {
class IntelVplEncoder;
struct IntelEncodedPacket;
namespace gpu {
class GpuFrameCore;
namespace intel {

/** Configuration retained across oneVPL encoder restarts. */
struct VplEncoderSequenceConfig {
    uint32_t codec = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t fps_num = 0;
    uint32_t fps_den = 0;
    uint32_t quality = 0;
    uint32_t keyframe_interval_frames = 0;
};

/**
 * @brief Coordinate CPU and external-GPU oneVPL encode sequences.
 *
 * A flush drains and closes the active hardware component. The caller restarts
 * CPU input after successfully consuming drained packets. External GPU mode may
 * only begin at a sequence boundary.
 */
class VplEncoderSequence final {
   public:
    /** Create the initial CPU-input encoder. */
    static std::unique_ptr<VplEncoderSequence> create(const VplEncoderSequenceConfig& config,
                                                      std::string& error);
    ~VplEncoderSequence();
    VplEncoderSequence(const VplEncoderSequence&) = delete;
    VplEncoderSequence& operator=(const VplEncoderSequence&) = delete;

    /** Submit one owned CPU NV12 frame. */
    mkvc_result write_nv12(const uint8_t* y, int32_t y_stride, const uint8_t* uv, int32_t uv_stride,
                           int64_t requested_pts, std::vector<IntelEncodedPacket>& packets,
                           std::string& error);

    /** Submit one compatible Intel GPU frame at the next sequence timestamp. */
    mkvc_result write_gpu(const std::shared_ptr<GpuFrameCore>& frame,
                          std::vector<IntelEncodedPacket>& packets, std::string& error);

    /** Drain and close the current sequence without consuming its packets. */
    mkvc_result flush(std::vector<IntelEncodedPacket>& packets, std::string& error);

    /** Restore CPU-input mode after drained packets were consumed successfully. */
    mkvc_result restart_cpu(std::string& error);

    /** Drain and destroy the current hardware component without restarting it. */
    mkvc_result close(std::vector<IntelEncodedPacket>& packets, std::string& error);

    /** Largest hardware pending depth observed across every restarted component. */
    uint32_t max_pending_observed() const noexcept;

   private:
    explicit VplEncoderSequence(VplEncoderSequenceConfig config);
    std::unique_ptr<IntelVplEncoder> create_cpu_encoder(std::string& error) const;
    void destroy_encoder() noexcept;

    VplEncoderSequenceConfig config_;
    std::unique_ptr<IntelVplEncoder> encoder_;
    int64_t next_pts_ = 0;
    uint64_t frames_in_sequence_ = 0;
    uint32_t hardware_pending_peak_ = 0;
    bool external_gpu_mode_ = false;
    bool closed_ = false;
};

}  // namespace intel
}  // namespace gpu
}  // namespace mkvc
