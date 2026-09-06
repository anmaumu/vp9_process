#include "intel_webm_encoder.hpp"

#include "gpu/gpu_frame.hpp"

#if defined(MKVC_HAS_INTEL_ONEVPL)
#include "encoder/cpu_frame_to_nv12.hpp"
#include "gpu/intel/vpl_encoder_sequence.hpp"
#include "gpu/intel/vpl_packet_muxer.hpp"
#endif

#include <limits>
#include <vector>

namespace mkvc {

struct IntelWebmEncoder::Impl {
#if defined(MKVC_HAS_INTEL_ONEVPL)
    std::unique_ptr<gpu::intel::VplPacketMuxer> muxer;
    std::unique_ptr<gpu::intel::VplEncoderSequence> sequence;
#endif
    uint32_t width = 0;
    uint32_t height = 0;
    bool closed = false;
    std::vector<uint8_t> i420;
    std::vector<uint8_t> nv12;
};

IntelWebmEncoder::IntelWebmEncoder() : impl_(std::make_unique<Impl>()) {}
IntelWebmEncoder::~IntelWebmEncoder() {
    std::string ignored;
    close(ignored);
}

std::unique_ptr<IntelWebmEncoder> IntelWebmEncoder::create(const mkvc_encoder_config& config,
                                                           std::string& error) {
#if !defined(MKVC_HAS_INTEL_ONEVPL)
    (void)config;
    error = "Intel oneVPL backend was not built";
    return nullptr;
#else
    auto encoder = std::unique_ptr<IntelWebmEncoder>(new IntelWebmEncoder());
    auto& impl = *encoder->impl_;
    impl.width = config.width;
    impl.height = config.height;
    const gpu::intel::VplEncoderSequenceConfig sequence_config{config.codec,
                                                               config.width,
                                                               config.height,
                                                               config.fps_num,
                                                               config.fps_den,
                                                               config.quality,
                                                               config.keyframe_interval_frames};
    impl.sequence = gpu::intel::VplEncoderSequence::create(sequence_config, error);
    if (!impl.sequence) return nullptr;
    impl.muxer = gpu::intel::VplPacketMuxer::create(config, error);
    if (!impl.muxer) return nullptr;
    const uint64_t y_size = static_cast<uint64_t>(config.width) * config.height;
    if (y_size * 3 / 2 > std::numeric_limits<size_t>::max()) {
        error = "Intel frame dimensions exceed addressable memory";
        return nullptr;
    }
    impl.i420.resize(static_cast<size_t>(y_size * 3 / 2));
    impl.nv12.resize(static_cast<size_t>(y_size * 3 / 2));
    return encoder;
#endif
}

mkvc_result IntelWebmEncoder::write(const mkvc_frame_view& frame, std::string& error) {
#if !defined(MKVC_HAS_INTEL_ONEVPL)
    (void)frame;
    error = "Intel oneVPL backend was not built";
    return MKVC_ERROR_NOT_SUPPORTED;
#else
    auto& impl = *impl_;
    if (impl.closed) {
        error = "Intel encoder is closed";
        return MKVC_ERROR_INVALID_STATE;
    }
    if (frame.width != impl.width || frame.height != impl.height) {
        error = "frame dimensions do not match Intel encoder";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    mkvc_result result = encoder::convert_cpu_frame_to_nv12(frame, impl.width, impl.height,
                                                            impl.i420, impl.nv12, "Intel", error);
    if (result != MKVC_OK) return result;
    const size_t y_size = static_cast<size_t>(impl.width) * impl.height;
    std::vector<IntelEncodedPacket> packets;
    result = impl.sequence->write_nv12(impl.nv12.data(), static_cast<int32_t>(impl.width),
                                       impl.nv12.data() + y_size, static_cast<int32_t>(impl.width),
                                       frame.pts, packets, error);
    if (result != MKVC_OK) return result;
    return impl.muxer->write(packets, error);
#endif
}

mkvc_result IntelWebmEncoder::write_gpu(const std::shared_ptr<gpu::GpuFrameCore>& frame,
                                        std::string& error) {
#if !defined(MKVC_HAS_INTEL_ONEVPL)
    (void)frame;
    error = "Intel oneVPL backend was not built";
    return MKVC_ERROR_NOT_SUPPORTED;
#else
    auto& impl = *impl_;
    if (impl.closed) {
        error = "Intel encoder is closed";
        return MKVC_ERROR_INVALID_STATE;
    }
    if (!frame || frame->desc().width != impl.width || frame->desc().height != impl.height) {
        error = "GPU frame dimensions do not match Intel encoder";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    std::vector<IntelEncodedPacket> packets;
    const mkvc_result result = impl.sequence->write_gpu(frame, packets, error);
    if (result != MKVC_OK) return result;
    return impl.muxer->write(packets, error);
#endif
}

mkvc_result IntelWebmEncoder::flush(std::string& error) {
#if !defined(MKVC_HAS_INTEL_ONEVPL)
    error = "Intel oneVPL backend was not built";
    return MKVC_ERROR_NOT_SUPPORTED;
#else
    auto& impl = *impl_;
    if (impl.closed) return MKVC_OK;
    std::vector<IntelEncodedPacket> packets;
    mkvc_result result = impl.sequence->flush(packets, error);
    if (result == MKVC_OK) result = impl.muxer->write(packets, error);
    if (result == MKVC_OK) result = impl.sequence->restart_cpu(error);
    return result;
#endif
}

mkvc_result IntelWebmEncoder::close(std::string& error) {
    if (impl_->closed) return MKVC_OK;
    mkvc_result result = MKVC_OK;
#if !defined(MKVC_HAS_INTEL_ONEVPL)
    (void)error;
#else
    auto& impl = *impl_;
    if (impl.sequence) {
        std::vector<IntelEncodedPacket> packets;
        result = impl.sequence->close(packets, error);
        if (result == MKVC_OK && impl.muxer) result = impl.muxer->write(packets, error);
    }
    if (result == MKVC_OK && impl.muxer) result = impl.muxer->finalize(error);
#endif
    impl_->closed = true;
    return result;
}

uint32_t IntelWebmEncoder::max_pending_observed() const {
#if defined(MKVC_HAS_INTEL_ONEVPL)
    return impl_->sequence ? impl_->sequence->max_pending_observed() : 0;
#else
    return 0;
#endif
}

}  // namespace mkvc
