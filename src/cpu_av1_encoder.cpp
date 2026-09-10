#include "cpu_av1_encoder.hpp"

#include <limits>
#include <string>

#include "cpu_av1_encoder_runtime.hpp"
#include "cpu_av1_encoder_state.hpp"
#include "encoder/cpu_frame_to_i420.hpp"
#include "webm_muxer.hpp"

namespace mkvc {

CpuAv1Encoder::CpuAv1Encoder() : impl_(std::make_unique<Impl>()) {}
CpuAv1Encoder::~CpuAv1Encoder() {
    std::string ignored;
    close(ignored);
}

std::unique_ptr<CpuAv1Encoder> CpuAv1Encoder::create(const mkvc_encoder_config& config,
                                                     std::string& error) {
#if !defined(MKVC_HAS_CPU_AV1)
    (void)config;
    error = "CPU AV1 backend was not built";
    return nullptr;
#else
    auto encoder = std::unique_ptr<CpuAv1Encoder>(new CpuAv1Encoder());
    auto& impl = *encoder->impl_;
    impl.width = config.width;
    impl.height = config.height;
    impl.timing.configure(config.fps_num, config.fps_den);
    impl.quality = config.quality;
    impl.keyframe_interval_frames = config.keyframe_interval_frames;
    if (cpu_av1_detail::initialize_codec(impl, error) != MKVC_OK) return nullptr;
    impl.muxer = WebmMuxer::create(config.output_path_utf8, MKVC_CODEC_AV1, config.width,
                                   config.height, config.fps_num, config.fps_den, error);
    if (!impl.muxer) return nullptr;
    const uint64_t y_size = static_cast<uint64_t>(config.width) * config.height;
    const uint64_t uv_size = static_cast<uint64_t>(config.width / 2) * (config.height / 2);
    if (y_size + 2 * uv_size > std::numeric_limits<size_t>::max()) {
        error = "frame dimensions exceed addressable memory";
        return nullptr;
    }
    impl.image.resize(static_cast<size_t>(y_size + 2 * uv_size));
    return encoder;
#endif
}

mkvc_result CpuAv1Encoder::write(const mkvc_frame_view& frame, std::string& error) {
#if !defined(MKVC_HAS_CPU_AV1)
    (void)frame;
    error = "CPU AV1 backend was not built";
    return MKVC_ERROR_NOT_SUPPORTED;
#else
    auto& impl = *impl_;
    if (impl.closed || impl.eos_sent) {
        error = "encoder is closed or draining";
        return MKVC_ERROR_INVALID_STATE;
    }
    if (frame.width != impl.width || frame.height != impl.height) {
        error = "frame dimensions do not match AV1 encoder configuration";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    const mkvc_result conversion = encoder::convert_cpu_frame_to_i420(
        frame, impl.width, impl.height, impl.image, "AV1", error);
    if (conversion != MKVC_OK) return conversion;
    return cpu_av1_detail::encode_frame(impl, frame.pts, error);
#endif
}

mkvc_result CpuAv1Encoder::flush(std::string& error) {
#if !defined(MKVC_HAS_CPU_AV1)
    error = "CPU AV1 backend was not built";
    return MKVC_ERROR_NOT_SUPPORTED;
#else
    if (impl_->closed) return MKVC_OK;
    if (impl_->frames_in_sequence == 0) return MKVC_OK;
    mkvc_result result = cpu_av1_detail::end_sequence(*impl_, error);
    cpu_av1_detail::destroy_codec(*impl_);
    if (result != MKVC_OK) return result;
    return cpu_av1_detail::initialize_codec(*impl_, error);
#endif
}

mkvc_result CpuAv1Encoder::close(std::string& error) {
#if !defined(MKVC_HAS_CPU_AV1)
    (void)error;
    impl_->closed = true;
    return MKVC_OK;
#else
    auto& impl = *impl_;
    if (impl.closed) return MKVC_OK;
    mkvc_result result = MKVC_OK;
    if (impl.codec_initialized && !impl.eos_sent)
        result = cpu_av1_detail::end_sequence(impl, error);
    if (result == MKVC_OK && impl.muxer) result = impl.muxer->finalize(error);
    cpu_av1_detail::destroy_codec(impl);
    impl.closed = true;
    return result;
#endif
}

}  // namespace mkvc
