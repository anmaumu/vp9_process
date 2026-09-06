#include "nvidia_webm_encoder.hpp"

#include "gpu/gpu_frame.hpp"
#include "nvidia_probe.hpp"
#include "webm_muxer.hpp"

#if defined(MKVC_HAS_NVIDIA)
#include <ffnvcodec/dynlink_cuda.h>

#include "gpu/nvidia/nvenc_api.hpp"
#include "gpu/nvidia/nvenc_cpu_conversion.hpp"
#include "gpu/nvidia/nvenc_cpu_submission.hpp"
#include "gpu/nvidia/nvenc_gpu_frame_validation.hpp"
#include "gpu/nvidia/nvenc_gpu_submission.hpp"
#include "gpu/nvidia/nvenc_packet_io.hpp"
#include "gpu/nvidia/nvenc_session.hpp"
#endif

#include <algorithm>
#include <limits>
#include <vector>

namespace mkvc {

struct NvidiaWebmEncoder::Impl {
#if defined(MKVC_HAS_NVIDIA)
    std::unique_ptr<gpu::nvidia::NvencApi> api;
    std::unique_ptr<gpu::nvidia::NvencSessionManager> session_manager;
    std::unique_ptr<WebmMuxer> muxer;
#endif
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t fps_num = 0;
    uint32_t fps_den = 0;
    uint32_t keyframe_interval = 0;
    uint64_t frame_index = 0;
    int64_t next_pts = 0;
    bool closed = false;
    std::vector<uint8_t> i420;
    std::vector<uint8_t> nv12;
};

NvidiaWebmEncoder::NvidiaWebmEncoder() : impl_(std::make_unique<Impl>()) {}
NvidiaWebmEncoder::~NvidiaWebmEncoder() {
    std::string ignored;
    close(ignored);
}

#if defined(MKVC_HAS_NVIDIA)
namespace {

/** Timestamp and picture flags selected before one synchronous submission. */
struct PictureTiming {
    int64_t pts_ns = 0;
    int64_t duration_ns = 0;
    bool force_keyframe = false;
};

mkvc_result mux_packet(NvidiaWebmEncoder::Impl& state, std::string& error) {
    const uint64_t default_duration =
        static_cast<uint64_t>(state.fps_den) * 1000000000ULL / state.fps_num;
    return gpu::nvidia::mux_nvenc_packet(*state.api, state.session_manager->session(), *state.muxer,
                                         default_duration, error);
}

/** Select monotonic fallback timing and the configured GOP boundary. */
PictureTiming select_picture_timing(const NvidiaWebmEncoder::Impl& state, int64_t requested_pts) {
    const int64_t duration =
        static_cast<int64_t>(static_cast<uint64_t>(state.fps_den) * 1000000000ULL / state.fps_num);
    return {requested_pts >= 0 ? requested_pts : state.next_pts, duration,
            state.frame_index % state.keyframe_interval == 0};
}

/** Commit counters only after both NVENC submission and container mux succeed. */
void commit_picture(NvidiaWebmEncoder::Impl& state, const PictureTiming& timing) {
    ++state.frame_index;
    state.next_pts = std::max(state.next_pts, timing.pts_ns + timing.duration_ns);
}

}  // namespace
#endif

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
    auto& state = *impl_;
    if (state.closed) {
        error = "NVIDIA encoder is closed";
        return MKVC_ERROR_INVALID_STATE;
    }
    gpu::nvidia::NvencCudaFrameView input;
    mkvc_result result =
        gpu::nvidia::prepare_nvenc_cuda_frame(frame, state.width, state.height, input, error);
    if (result != MKVC_OK) return result;
    const auto source_context =
        reinterpret_cast<CUcontext>(static_cast<uintptr_t>(input.context_handle));
    result = state.session_manager->bind_cuda_context(*state.api, source_context, frame,
                                                      state.frame_index, error);
    if (result != MKVC_OK) return result;

    const PictureTiming timing = select_picture_timing(state, input.pts_ns);
    result = gpu::nvidia::submit_nvenc_cuda_frame(
        *state.api, state.session_manager->session(), input.cuda_array, input.resource_handle,
        state.width, state.height, input.pitch, state.frame_index, timing.pts_ns,
        timing.duration_ns, timing.force_keyframe, error);
    if (result == MKVC_OK) result = mux_packet(state, error);
    if (result == MKVC_OK) commit_picture(state, timing);
    return result;
#endif
}

mkvc_result NvidiaWebmEncoder::write(const mkvc_frame_view& frame, std::string& error) {
#if !defined(MKVC_HAS_NVIDIA)
    (void)frame;
    error = "NVIDIA backend was not built";
    return MKVC_ERROR_NOT_SUPPORTED;
#else
    auto& state = *impl_;
    if (state.closed) {
        error = "NVIDIA encoder is closed";
        return MKVC_ERROR_INVALID_STATE;
    }
    if (frame.width != state.width || frame.height != state.height) {
        error = "frame dimensions do not match NVIDIA encoder";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    mkvc_result result = gpu::nvidia::convert_nvenc_input_to_nv12(frame, state.width, state.height,
                                                                  state.i420, state.nv12, error);
    if (result != MKVC_OK) return result;
    const PictureTiming timing = select_picture_timing(state, frame.pts);
    result = gpu::nvidia::submit_nvenc_cpu_frame(
        *state.api, state.session_manager->session(), state.nv12.data(), state.width, state.height,
        state.frame_index, timing.pts_ns, timing.duration_ns, timing.force_keyframe, error);
    if (result != MKVC_OK) return result;
    result = mux_packet(state, error);
    if (result == MKVC_OK) commit_picture(state, timing);
    return result;
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
