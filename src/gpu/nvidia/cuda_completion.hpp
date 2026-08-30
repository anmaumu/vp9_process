#pragma once

#include "../gpu_frame.hpp"

#include <ffnvcodec/dynlink_cuda.h>

#include <memory>

namespace mkvc::gpu::nvidia {

struct CudaCompletionFunctions {
    tcuCtxPushCurrent_v2* context_push = nullptr;
    tcuCtxPopCurrent_v2* context_pop = nullptr;
    tcuEventQuery* event_query = nullptr;
};

/** CUDA event completion without device-wide synchronization. */
std::shared_ptr<Completion> make_cuda_event_completion(
    CUcontext context, CUevent event, CudaCompletionFunctions functions,
    std::shared_ptr<void> context_keepalive = {});

/** Dynamically load the CUDA driver and create an event-backed completion. */
mkvc_result load_cuda_event_completion(
    uint64_t context, uint64_t event,
    std::shared_ptr<Completion>& completion, std::string& error);

}  // namespace mkvc::gpu::nvidia
