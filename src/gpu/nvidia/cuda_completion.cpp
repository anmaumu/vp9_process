#include "cuda_completion.hpp"

#include <utility>

namespace mkvc::gpu::nvidia {

std::shared_ptr<Completion> make_cuda_event_completion(
    CUcontext context, CUevent event, CudaCompletionFunctions functions,
    std::shared_ptr<void> context_keepalive) {
    return std::make_shared<CallbackCompletion>(
        [context, event, functions, keepalive = std::move(context_keepalive)](
            bool& complete, std::string& error) {
            (void)keepalive;
            if (context == nullptr || event == nullptr ||
                functions.context_push == nullptr || functions.context_pop == nullptr ||
                functions.event_query == nullptr) {
                error = "invalid CUDA event completion";
                return MKVC_ERROR_INVALID_ARGUMENT;
            }
            if (functions.context_push(context) != CUDA_SUCCESS) {
                error = "failed to activate CUDA context for event query";
                return MKVC_ERROR_CODEC;
            }
            const CUresult queried = functions.event_query(event);
            CUcontext popped = nullptr;
            const CUresult pop_result = functions.context_pop(&popped);
            if (pop_result != CUDA_SUCCESS || popped != context) {
                error = "failed to release CUDA context after event query";
                return MKVC_ERROR_CODEC;
            }
            if (queried == CUDA_SUCCESS) { complete = true; return MKVC_OK; }
            if (queried == CUDA_ERROR_NOT_READY) { complete = false; return MKVC_OK; }
            error = "CUDA event query failed";
            return MKVC_ERROR_CODEC;
        });
}

}  // namespace mkvc::gpu::nvidia
