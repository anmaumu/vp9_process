#include "cpu_vp9_encoder.hpp"

#include <limits>

#include "cpu_vp9_encoder_runtime.hpp"
#include "cpu_vp9_encoder_state.hpp"
#include "encoder/cpu_frame_to_i420.hpp"
#include "webm_muxer.hpp"

namespace mkvc {

CpuVp9Encoder::CpuVp9Encoder() : impl_(std::make_unique<Impl>()) {}

CpuVp9Encoder::~CpuVp9Encoder() {
    std::string ignored;
    close(ignored);
}

std::unique_ptr<CpuVp9Encoder> CpuVp9Encoder::create(const mkvc_encoder_config& config,
                                                     std::string& error) {
#if !defined(MKVC_HAS_CPU_VP9)
    (void)config;
    error = "CPU VP9 backend was not built";
    return nullptr;
#else
    auto encoder = std::unique_ptr<CpuVp9Encoder>(new CpuVp9Encoder());
    auto& impl = *encoder->impl_;
    impl.width = config.width;
    impl.height = config.height;
    impl.timing.configure(config.fps_num, config.fps_den);

    if (cpu_vp9_detail::initialize_codec(impl, config, error) != MKVC_OK) return nullptr;

    impl.muxer = WebmMuxer::create(config.output_path_utf8, MKVC_CODEC_VP9, config.width,
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

mkvc_result CpuVp9Encoder::write(const mkvc_frame_view& frame, std::string& error) {
#if !defined(MKVC_HAS_CPU_VP9)
    (void)frame;
    error = "CPU VP9 backend was not built";
    return MKVC_ERROR_NOT_SUPPORTED;
#else
    auto& impl = *impl_;
    if (impl.closed) {
        error = "encoder is closed";
        return MKVC_ERROR_INVALID_STATE;
    }
    if (frame.width != impl.width || frame.height != impl.height) {
        error = "frame dimensions do not match encoder configuration";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }

    const mkvc_result conversion = encoder::convert_cpu_frame_to_i420(
        frame, impl.width, impl.height, impl.image, "VP9", error);
    if (conversion != MKVC_OK) return conversion;

    return cpu_vp9_detail::encode_frame(impl, frame.pts, error);
#endif
}

mkvc_result CpuVp9Encoder::flush(std::string& error) {
#if !defined(MKVC_HAS_CPU_VP9)
    error = "CPU VP9 backend was not built";
    return MKVC_ERROR_NOT_SUPPORTED;
#else
    if (impl_->closed) {
        return MKVC_OK;
    }
    return cpu_vp9_detail::flush_codec(*impl_, error);
#endif
}

mkvc_result CpuVp9Encoder::close(std::string& error) {
#if !defined(MKVC_HAS_CPU_VP9)
    (void)error;
    impl_->closed = true;
    return MKVC_OK;
#else
    if (impl_->closed) {
        return MKVC_OK;
    }
    mkvc_result result = MKVC_OK;
    if (impl_->codec_initialized) {
        result = flush(error);
    }
    if (result == MKVC_OK && impl_->muxer) result = impl_->muxer->finalize(error);
    cpu_vp9_detail::destroy_codec(*impl_);
    impl_->closed = true;
    return result;
#endif
}

}  // namespace mkvc
