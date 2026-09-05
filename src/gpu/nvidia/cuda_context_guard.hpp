#pragma once

#include <ffnvcodec/dynlink_cuda.h>

namespace mkvc::gpu::nvidia {

/**
 * @brief Scope a borrowed CUDA driver context activation.
 *
 * The guard never owns or destroys the context. Construction pushes the
 * supplied context when both driver functions are valid. release() performs
 * the matching pop and verifies that CUDA returned the same context. The
 * destructor provides best-effort cleanup for every early-return path.
 */
class CudaContextGuard final {
   public:
    CudaContextGuard(CUcontext context, tcuCtxPushCurrent_v2* push,
                     tcuCtxPopCurrent_v2* pop) noexcept
        : context_(context), pop_(pop) {
        active_ = context_ != nullptr && push != nullptr && pop_ != nullptr &&
                  push(context_) == CUDA_SUCCESS;
    }

    CudaContextGuard(const CudaContextGuard&) = delete;
    CudaContextGuard& operator=(const CudaContextGuard&) = delete;

    ~CudaContextGuard() {
        if (!active_) return;
        CUcontext ignored = nullptr;
        (void)pop_(&ignored);
    }

    /** @return true when the requested context was pushed successfully. */
    explicit operator bool() const noexcept { return active_; }

    /**
     * @brief Pop the context now and validate the push/pop pairing.
     * @return true only when CUDA popped the context supplied at construction.
     */
    bool release() noexcept {
        if (!active_) return false;
        active_ = false;
        CUcontext popped = nullptr;
        return pop_(&popped) == CUDA_SUCCESS && popped == context_;
    }

   private:
    CUcontext context_ = nullptr;
    tcuCtxPopCurrent_v2* pop_ = nullptr;
    bool active_ = false;
};

}  // namespace mkvc::gpu::nvidia
