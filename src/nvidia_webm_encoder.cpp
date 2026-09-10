#include "nvidia_webm_encoder.hpp"

#include "gpu/nvidia/nvenc_encoder_submission.hpp"
#include "nvidia_probe.hpp"
#include "nvidia_webm_encoder_state.hpp"

#if defined(MKVC_HAS_NVIDIA)
#include "gpu/nvidia/nvenc_api.hpp"
#include "gpu/nvidia/nvenc_packet_io.hpp"
#include "gpu/nvidia/nvenc_session.hpp"
#endif

#include <algorithm>
#include <limits>

namespace mkvc {

NvidiaWebmEncoder::NvidiaWebmEncoder() : impl_(std::make_unique<Impl>()) {}
NvidiaWebmEncoder::~NvidiaWebmEncoder() {
    std::string ignored;
    close(ignored);
}

std::unique_ptr<NvidiaWebmEncoder> NvidiaWebmEncoder::create(const mkvc_encoder_config& config,
                                                             std::string& error) {
#if !defined(MKVC_HAS_NVIDIA)
    (void)config;
    error = "NVIDIA backend was not built";
    return nullptr;
#else
    if (config.codec != MKVC_CODEC_AV1 || !probe_nvidia().av1_encode) {
        error = "NVIDIA AV1 encode is unavailable";
        return nullptr;
    }
    auto result = std::unique_ptr<NvidiaWebmEncoder>(new NvidiaWebmEncoder());
    auto& state = *result->impl_;
    state.width = config.width;
    state.height = config.height;
    state.fps_num = config.fps_num;
    state.fps_den = config.fps_den;
    state.keyframe_interval = config.keyframe_interval_frames == 0
                                  ? std::max(1u, 4u * config.fps_num / config.fps_den)
                                  : config.keyframe_interval_frames;
    state.api = gpu::nvidia::NvencApi::load(error);
    if (!state.api) return nullptr;
    const gpu::nvidia::NvencSessionConfig session_config{state.width,    state.height,
                                                         state.fps_num,  state.fps_den,
                                                         config.quality, state.keyframe_interval};
    state.session_manager = std::make_unique<gpu::nvidia::NvencSessionManager>(session_config);
    if (state.session_manager->initialize_cpu(*state.api, error) != MKVC_OK) return nullptr;
    state.muxer = WebmMuxer::create(config.output_path_utf8, MKVC_CODEC_AV1, config.width,
                                    config.height, config.fps_num, config.fps_den, error);
    if (!state.muxer) return nullptr;
    const uint64_t y_size = static_cast<uint64_t>(config.width) * config.height;
    if (y_size * 3 / 2 > std::numeric_limits<size_t>::max()) {
        error = "NVIDIA frame dimensions exceed memory";
        return nullptr;
    }
    state.i420.resize(static_cast<size_t>(y_size * 3 / 2));
    state.nv12.resize(static_cast<size_t>(y_size * 3 / 2));
    return result;
#endif
}

mkvc_result NvidiaWebmEncoder::write_gpu(const std::shared_ptr<gpu::GpuFrameCore>& frame,
                                         std::string& error) {
#if !defined(MKVC_HAS_NVIDIA)
    (void)frame;
    error = "NVIDIA backend was not built";
    return MKVC_ERROR_NOT_SUPPORTED;
#else
    return gpu::nvidia::write_nvenc_gpu_frame(*impl_, frame, error);
#endif
}

mkvc_result NvidiaWebmEncoder::write(const mkvc_frame_view& frame, std::string& error) {
#if !defined(MKVC_HAS_NVIDIA)
    (void)frame;
    error = "NVIDIA backend was not built";
    return MKVC_ERROR_NOT_SUPPORTED;
#else
    return gpu::nvidia::write_nvenc_cpu_frame(*impl_, frame, error);
#endif
}

mkvc_result NvidiaWebmEncoder::flush(std::string& error) {
#if !defined(MKVC_HAS_NVIDIA)
    error = "NVIDIA backend was not built";
    return MKVC_ERROR_NOT_SUPPORTED;
#else
    auto& state = *impl_;
    if (state.closed || state.frame_index == 0) return MKVC_OK;
    const mkvc_result drain_result =
        gpu::nvidia::drain_nvenc_session(*state.api, state.session_manager->session(), error);
    if (drain_result != MKVC_OK) return drain_result;
    state.frame_index = 0;
    return state.session_manager->restart_cpu(*state.api, error);
#endif
}

mkvc_result NvidiaWebmEncoder::close(std::string& error) {
    if (impl_->closed) return MKVC_OK;
    mkvc_result result = MKVC_OK;
#if !defined(MKVC_HAS_NVIDIA)
    (void)error;
#else
    auto& state = *impl_;
    if (state.api && state.session_manager && state.session_manager->session().encoder != nullptr &&
        state.frame_index > 0) {
        result =
            gpu::nvidia::drain_nvenc_session(*state.api, state.session_manager->session(), error);
    }
    if (state.api && state.session_manager) state.session_manager->destroy(*state.api);
    if (result == MKVC_OK && state.muxer) result = state.muxer->finalize(error);
#endif
    impl_->closed = true;
    return result;
}

uint32_t NvidiaWebmEncoder::max_pending_observed() const { return 1; }

}  // namespace mkvc
