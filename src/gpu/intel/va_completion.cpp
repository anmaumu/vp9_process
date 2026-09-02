#include "va_completion.hpp"

#include <limits>
#include <mutex>
#include <utility>

#if defined(__linux__) && defined(MKVC_HAS_INTEL_ONEVPL)
#include <dlfcn.h>
#endif

namespace mkvc::gpu::intel {

std::shared_ptr<Completion> make_va_surface_completion(
    void* display, uint32_t surface, VaSyncSurface2 sync,
    std::shared_ptr<void> library_keepalive) {
    struct State {
        std::mutex mutex;
        bool terminal = false;
        mkvc_result result = MKVC_OK;
        std::string error;
    };
    auto state = std::make_shared<State>();
    return std::make_shared<CallbackCompletion>(
        [display, surface, sync, state, keepalive = std::move(library_keepalive)](
            bool& complete, std::string& error) {
            (void)keepalive;
            std::lock_guard<std::mutex> lock(state->mutex);
            if (!state->terminal) {
                if (display == nullptr || surface == UINT32_MAX || sync == nullptr) {
                    state->result = MKVC_ERROR_INVALID_ARGUMENT;
                    state->error = "invalid VA surface synchronization arguments";
                } else {
                    const int status = sync(display, surface, 0);
                    if (status == kVaTimedOut) {
                        complete = false;
                        return MKVC_OK;
                    }
                    if (status != kVaSuccess) {
                        state->result = status == kVaUnimplemented
                            ? MKVC_ERROR_NOT_SUPPORTED : MKVC_ERROR_CODEC;
                        state->error = "vaSyncSurface2 failed with status " +
                            std::to_string(status);
                    }
                }
                state->terminal = true;
            }
            complete = state->result == MKVC_OK;
            error = state->error;
            return state->result;
        });
}

mkvc_result load_va_surface_completion(
    uint64_t display, uint64_t surface,
    std::shared_ptr<Completion>& completion, std::string& error) {
    completion.reset();
    if (display == 0 || display > std::numeric_limits<uintptr_t>::max() ||
        surface >= UINT32_MAX) {
        error = "VA synchronization requires a valid display and surface ID";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
#if defined(__linux__) && defined(MKVC_HAS_INTEL_ONEVPL)
    auto library = std::shared_ptr<void>(
        dlopen("libva.so.2", RTLD_NOW | RTLD_LOCAL),
        [](void* handle) { if (handle) dlclose(handle); });
    if (!library) {
        error = "libva.so.2 is unavailable";
        return MKVC_ERROR_NOT_SUPPORTED;
    }
    const auto sync = reinterpret_cast<VaSyncSurface2>(
        dlsym(library.get(), "vaSyncSurface2"));
    if (!sync) {
        error = "libva lacks vaSyncSurface2; blocking fallback is disabled";
        return MKVC_ERROR_NOT_SUPPORTED;
    }
    auto candidate = make_va_surface_completion(
        reinterpret_cast<void*>(static_cast<uintptr_t>(display)),
        static_cast<uint32_t>(surface), sync, std::move(library));
    // Detect a driver that exports libva's entry point but cannot implement it.
    // A zero timeout is a probe, not a host-wide or device-wide wait.
    const mkvc_result result = candidate->wait(0, error);
    if (result != MKVC_OK && result != MKVC_ERROR_TIMEOUT) return result;
    completion = std::move(candidate);
    return MKVC_OK;
#else
    error = "native VA surface synchronization requires a Linux Intel build";
    return MKVC_ERROR_NOT_SUPPORTED;
#endif
}

}  // namespace mkvc::gpu::intel
