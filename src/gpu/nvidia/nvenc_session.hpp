#pragma once

#include <ffnvcodec/dynlink_cuda.h>
#include <ffnvcodec/nvEncodeAPI.h>

#include <cstdint>
#include <memory>
#include <string>

#include "mkvcodec/mkvc.h"

namespace mkvc::gpu {

class GpuFrameCore;

namespace nvidia {

class NvencApi;

/** Immutable settings used to initialize one synchronous NVENC AV1 session. */
struct NvencSessionConfig {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t fps_num = 0;
    uint32_t fps_den = 0;
    uint32_t quality = 0;
    uint32_t keyframe_interval = 0;
};

/** Native resources owned or borrowed by one NVENC encoding session. */
struct NvencSession {
    CUcontext context = nullptr;
    bool owns_context = false;
    void* encoder = nullptr;
    NV_ENC_INPUT_PTR input = nullptr;
    NV_ENC_OUTPUT_PTR output = nullptr;
};

/**
 * @brief Create a synchronous P4 constant-QP AV1 encoding session.
 *
 * @param api Loaded CUDA/NVENC function table.
 * @param config Frame geometry, rate, quality and GOP settings.
 * @param external_context Borrowed CUDA context, or null to create an owned one.
 * @param session Receives native session resources; must initially be empty.
 * @param error Receives the first failed initialization stage.
 * @return MKVC_OK or MKVC_ERROR_CODEC.
 */
mkvc_result initialize_nvenc_session(NvencApi& api, const NvencSessionConfig& config,
                                     CUcontext external_context, NvencSession& session,
                                     std::string& error);

/**
 * @brief Destroy input, bitstream, encoder and owned-context resources.
 * @param api Loaded CUDA/NVENC function table.
 * @param session Session to clear; borrowed CUDA contexts are never destroyed.
 */
void destroy_nvenc_session(NvencApi& api, NvencSession& session) noexcept;

/**
 * @brief Own NVENC session context binding and its first external-frame lease.
 *
 * The manager starts with an internally owned CUDA context for CPU input. Before
 * the first picture it may replace that session with one borrowing a CUDA frame
 * context. It rejects later context changes, destroys the session before releasing
 * the context owner, and cleans partial initialization failures.
 */
class NvencSessionManager final {
   public:
    /** Construct an empty manager with immutable encoder settings. */
    explicit NvencSessionManager(NvencSessionConfig config) : config_(config) {}

    /** Create the initial internally owned CUDA/NVENC session. */
    mkvc_result initialize_cpu(NvencApi& api, std::string& error);

    /**
     * @brief Bind the session to the CUDA context of the first GPU frame.
     * @param api Loaded CUDA/NVENC function table.
     * @param context Borrowed CUDA context from the validated frame.
     * @param context_owner Frame lease retaining the borrowed context owner.
     * @param submitted_frames Pictures already accepted by the current session.
     * @param error Receives transition or initialization diagnostics.
     */
    mkvc_result bind_cuda_context(NvencApi& api, CUcontext context,
                                  const std::shared_ptr<GpuFrameCore>& context_owner,
                                  uint64_t submitted_frames, std::string& error);

    /** Destroy the current binding and recreate an owned CPU-input session. */
    mkvc_result restart_cpu(NvencApi& api, std::string& error);

    /** Destroy native resources before releasing any borrowed context owner. */
    void destroy(NvencApi& api) noexcept;

    /** Borrow the current initialized session for submission or packet access. */
    const NvencSession& session() const noexcept { return session_; }

   private:
    mkvc_result initialize(NvencApi& api, CUcontext external_context, std::string& error);

    NvencSessionConfig config_{};
    CUcontext external_context_ = nullptr;
    NvencSession session_{};
    std::shared_ptr<GpuFrameCore> context_owner_;
};

}  // namespace nvidia
}  // namespace mkvc::gpu
