/**
 * @file gpu_external_import_nvidia.cpp
 * @brief NVIDIA CUDA-event external-frame C adapter.
 */
#include "gpu_external_import.hpp"

#if defined(MKVC_HAS_NVIDIA)
#include "nvidia/cuda_completion.hpp"
#endif

#include <memory>
#include <string>
#include <utility>

extern thread_local std::string mkvc_last_error;

namespace external = mkvc::gpu::external;

extern "C" {

mkvc_result mkvc_gpu_frame_import_cuda_event(const mkvc_gpu_external_frame_config* config,
                                             mkvc_gpu_frame** out_frame) {
    mkvc_last_error.clear();
    if (config == nullptr || out_frame == nullptr || config->struct_size < sizeof(*config) ||
        config->struct_version != 1) {
        return external::fail(MKVC_ERROR_INVALID_ARGUMENT,
                              "invalid CUDA event frame configuration");
    }
    *out_frame = nullptr;
    std::string error;
    if (!external::valid_layout(*config, error))
        return external::fail(MKVC_ERROR_INVALID_ARGUMENT, std::move(error));
    if (config->frame.backend != MKVC_BACKEND_NVIDIA || config->query != nullptr ||
        config->native_handle.handles[3] == 0) {
        return external::fail(
            MKVC_ERROR_INVALID_ARGUMENT,
            "CUDA event import requires NVIDIA, a native event, and no query callback");
    }
#if defined(MKVC_HAS_NVIDIA)
    std::shared_ptr<mkvc::gpu::Completion> producer;
    const mkvc_result result = mkvc::gpu::nvidia::load_cuda_event_completion(
        config->native_handle.handles[1], config->native_handle.handles[3], producer, error);
    if (result != MKVC_OK) return external::fail(result, std::move(error));
    return external::import_with_completion(*config, std::move(producer), out_frame);
#else
    return external::fail(MKVC_ERROR_NOT_SUPPORTED,
                          "CUDA event import was not enabled in this build");
#endif
}

}  // extern "C"
