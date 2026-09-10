#include "gpu/nvidia/nvenc_encoder_submission.hpp"

#if defined(MKVC_HAS_NVIDIA)
#include <ffnvcodec/dynlink_cuda.h>

#include <algorithm>
#include <cstdint>

#include "gpu/gpu_frame.hpp"
#include "gpu/nvidia/nvenc_cpu_conversion.hpp"
#include "gpu/nvidia/nvenc_cpu_submission.hpp"
#include "gpu/nvidia/nvenc_gpu_frame_validation.hpp"
#include "gpu/nvidia/nvenc_gpu_submission.hpp"
#include "gpu/nvidia/nvenc_packet_io.hpp"
#include "nvidia_webm_encoder_state.hpp"

namespace mkvc::gpu::nvidia {
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
    return mux_nvenc_packet(*state.api, state.session_manager->session(), *state.muxer,
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

mkvc_result write_nvenc_gpu_frame(NvidiaWebmEncoder::Impl& state,
                                  const std::shared_ptr<GpuFrameCore>& frame, std::string& error) {
    if (state.closed) {
        error = "NVIDIA encoder is closed";
        return MKVC_ERROR_INVALID_STATE;
    }
    NvencCudaFrameView input;
    mkvc_result result = prepare_nvenc_cuda_frame(frame, state.width, state.height, input, error);
    if (result != MKVC_OK) return result;
    const auto source_context =
        reinterpret_cast<CUcontext>(static_cast<uintptr_t>(input.context_handle));
    result = state.session_manager->bind_cuda_context(*state.api, source_context, frame,
                                                      state.frame_index, error);
    if (result != MKVC_OK) return result;

    const PictureTiming timing = select_picture_timing(state, input.pts_ns);
    result = submit_nvenc_cuda_frame(*state.api, state.session_manager->session(), input.cuda_array,
                                     input.resource_handle, state.width, state.height, input.pitch,
                                     state.frame_index, timing.pts_ns, timing.duration_ns,
                                     timing.force_keyframe, error);
    if (result == MKVC_OK) result = mux_packet(state, error);
    if (result == MKVC_OK) commit_picture(state, timing);
    return result;
}

mkvc_result write_nvenc_cpu_frame(NvidiaWebmEncoder::Impl& state, const mkvc_frame_view& frame,
                                  std::string& error) {
    if (state.closed) {
        error = "NVIDIA encoder is closed";
        return MKVC_ERROR_INVALID_STATE;
    }
    if (frame.width != state.width || frame.height != state.height) {
        error = "frame dimensions do not match NVIDIA encoder";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    mkvc_result result = convert_nvenc_input_to_nv12(frame, state.width, state.height, state.i420,
                                                     state.nv12, error);
    if (result != MKVC_OK) return result;
    const PictureTiming timing = select_picture_timing(state, frame.pts);
    result = submit_nvenc_cpu_frame(*state.api, state.session_manager->session(), state.nv12.data(),
                                    state.width, state.height, state.frame_index, timing.pts_ns,
                                    timing.duration_ns, timing.force_keyframe, error);
    if (result != MKVC_OK) return result;
    result = mux_packet(state, error);
    if (result == MKVC_OK) commit_picture(state, timing);
    return result;
}

}  // namespace mkvc::gpu::nvidia
#endif
