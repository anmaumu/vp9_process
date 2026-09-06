#pragma once

#include <ffnvcodec/dynlink_cuda.h>
#include <ffnvcodec/nvEncodeAPI.h>

#include <cstdint>
#include <string>

#include "mkvcodec/mkvc.h"

namespace mkvc::gpu::nvidia {

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

}  // namespace mkvc::gpu::nvidia
