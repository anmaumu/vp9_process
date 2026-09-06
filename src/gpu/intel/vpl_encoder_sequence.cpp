#include "gpu/intel/vpl_encoder_sequence.hpp"

#include <algorithm>
#include <utility>

#include "gpu/gpu_frame.hpp"
#include "intel_vpl_encoder.hpp"

namespace mkvc::gpu::intel {

VplEncoderSequence::VplEncoderSequence(VplEncoderSequenceConfig config) : config_(config) {}

VplEncoderSequence::~VplEncoderSequence() {
    std::vector<IntelEncodedPacket> ignored_packets;
    std::string ignored_error;
    close(ignored_packets, ignored_error);
}

std::unique_ptr<VplEncoderSequence> VplEncoderSequence::create(
    const VplEncoderSequenceConfig& config, std::string& error) {
    auto result = std::unique_ptr<VplEncoderSequence>(new VplEncoderSequence(config));
    result->encoder_ = result->create_cpu_encoder(error);
    if (!result->encoder_) return nullptr;
    return result;
}

std::unique_ptr<IntelVplEncoder> VplEncoderSequence::create_cpu_encoder(std::string& error) const {
    return IntelVplEncoder::create(config_.codec, config_.width, config_.height, config_.fps_num,
                                   config_.fps_den, config_.quality,
                                   config_.keyframe_interval_frames, error);
}

mkvc_result VplEncoderSequence::write_nv12(const uint8_t* y, int32_t y_stride, const uint8_t* uv,
                                           int32_t uv_stride, int64_t requested_pts,
                                           std::vector<IntelEncodedPacket>& packets,
                                           std::string& error) {
    if (closed_) {
        error = "Intel encoder is closed";
        return MKVC_ERROR_INVALID_STATE;
    }
    const int64_t frame_pts = requested_pts >= 0 ? requested_pts : next_pts_;
    const mkvc_result result =
        encoder_->write_nv12(y, y_stride, uv, uv_stride, frame_pts, packets, error);
    if (result != MKVC_OK) return result;
    next_pts_ = std::max(next_pts_, frame_pts + 1);
    ++frames_in_sequence_;
    return MKVC_OK;
}

mkvc_result VplEncoderSequence::write_gpu(const std::shared_ptr<GpuFrameCore>& frame,
                                          std::vector<IntelEncodedPacket>& packets,
                                          std::string& error) {
    if (closed_) {
        error = "Intel encoder is closed";
        return MKVC_ERROR_INVALID_STATE;
    }
    if (frame->backend_resource().kind == BackendResourceKind::kNone && !external_gpu_mode_) {
        if (frames_in_sequence_ != 0) {
            error = "flush Intel writer before switching to external GPU input";
            return MKVC_ERROR_INVALID_STATE;
        }
        auto adapter = IntelVplEncoder::create(config_.codec, config_.width, config_.height,
                                               config_.fps_num, config_.fps_den, config_.quality,
                                               config_.keyframe_interval_frames, error, 4, frame);
        if (!adapter) return MKVC_ERROR_NOT_SUPPORTED;
        destroy_encoder();
        encoder_ = std::move(adapter);
        external_gpu_mode_ = true;
    }
    const int64_t frame_pts = next_pts_;
    const mkvc_result result = encoder_->write_gpu_surface(frame, frame_pts, packets, error);
    if (result != MKVC_OK) return result;
    next_pts_ = frame_pts + 1;
    ++frames_in_sequence_;
    return MKVC_OK;
}

mkvc_result VplEncoderSequence::flush(std::vector<IntelEncodedPacket>& packets,
                                      std::string& error) {
    packets.clear();
    if (closed_ || (frames_in_sequence_ == 0 && !external_gpu_mode_)) return MKVC_OK;
    const mkvc_result result = encoder_->drain(packets, error);
    destroy_encoder();
    frames_in_sequence_ = 0;
    external_gpu_mode_ = false;
    return result;
}

mkvc_result VplEncoderSequence::restart_cpu(std::string& error) {
    if (closed_) {
        error = "Intel encoder is closed";
        return MKVC_ERROR_INVALID_STATE;
    }
    if (encoder_) return MKVC_OK;
    encoder_ = create_cpu_encoder(error);
    return encoder_ ? MKVC_OK : MKVC_ERROR_CODEC;
}

mkvc_result VplEncoderSequence::close(std::vector<IntelEncodedPacket>& packets,
                                      std::string& error) {
    packets.clear();
    if (closed_) return MKVC_OK;
    mkvc_result result = MKVC_OK;
    if (encoder_ && frames_in_sequence_ > 0) result = encoder_->drain(packets, error);
    destroy_encoder();
    closed_ = true;
    return result;
}

void VplEncoderSequence::destroy_encoder() noexcept {
    if (!encoder_) return;
    hardware_pending_peak_ = std::max(hardware_pending_peak_, encoder_->max_pending_observed());
    std::string ignored;
    encoder_->close(ignored);
    encoder_.reset();
}

uint32_t VplEncoderSequence::max_pending_observed() const noexcept {
    return std::max(hardware_pending_peak_, encoder_ ? encoder_->max_pending_observed() : 0);
}

}  // namespace mkvc::gpu::intel
