#include "cuda_completion.hpp"

#include <mutex>
#include <utility>

#include "cuda_context_guard.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace mkvc::gpu::nvidia {

namespace {
class CudaDriverLibrary {
   public:
#ifdef _WIN32
    using Handle = HMODULE;
#else
    using Handle = void*;
#endif

    ~CudaDriverLibrary() {
        if (handle_ == nullptr) return;
#ifdef _WIN32
        FreeLibrary(handle_);
#else
        dlclose(handle_);
#endif
    }

    bool open(std::string& error) {
#ifdef _WIN32
        handle_ = LoadLibraryA("nvcuda.dll");
#else
        handle_ = dlopen("libcuda.so.1", RTLD_NOW | RTLD_LOCAL);
#endif
        if (handle_ == nullptr) {
            error = "CUDA driver library is unavailable";
            return false;
        }
        return true;
    }

    template <typename T>
    T symbol(const char* name) const {
#ifdef _WIN32
        return reinterpret_cast<T>(GetProcAddress(handle_, name));
#else
        return reinterpret_cast<T>(dlsym(handle_, name));
#endif
    }

   private:
    Handle handle_ = nullptr;
};

std::shared_ptr<CudaDriverLibrary> load_cuda_driver(std::string& error) {
    static std::mutex mutex;
    static std::shared_ptr<CudaDriverLibrary> cached;
    std::lock_guard<std::mutex> lock(mutex);
    if (cached) return cached;
    auto library = std::make_shared<CudaDriverLibrary>();
    if (!library->open(error)) return {};
    cached = library;
    return library;
}
}  // namespace

std::shared_ptr<Completion> make_cuda_event_completion(CUcontext context, CUevent event,
                                                       CudaCompletionFunctions functions,
                                                       std::shared_ptr<void> context_keepalive) {
    return std::make_shared<CallbackCompletion>(
        [context, event, functions, keepalive = std::move(context_keepalive)](bool& complete,
                                                                              std::string& error) {
            (void)keepalive;
            if (context == nullptr || event == nullptr || functions.context_push == nullptr ||
                functions.context_pop == nullptr || functions.event_query == nullptr) {
                error = "invalid CUDA event completion";
                return MKVC_ERROR_INVALID_ARGUMENT;
            }
            CudaContextGuard context_guard(context, functions.context_push, functions.context_pop);
            if (!context_guard) {
                error = "failed to activate CUDA context for event query";
                return MKVC_ERROR_CODEC;
            }
            const CUresult queried = functions.event_query(event);
            if (!context_guard.release()) {
                error = "failed to release CUDA context after event query";
                return MKVC_ERROR_CODEC;
            }
            if (queried == CUDA_SUCCESS) {
                complete = true;
                return MKVC_OK;
            }
            if (queried == CUDA_ERROR_NOT_READY) {
                complete = false;
                return MKVC_OK;
            }
            error = "CUDA event query failed";
            return MKVC_ERROR_CODEC;
        });
}

mkvc_result load_cuda_event_completion(uint64_t context, uint64_t event,
                                       std::shared_ptr<Completion>& completion,
                                       std::string& error) {
    completion.reset();
    if (context == 0 || event == 0) {
        error = "CUDA context and event must be nonzero";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    auto library = load_cuda_driver(error);
    if (!library) return MKVC_ERROR_NOT_SUPPORTED;
    const auto init = library->symbol<tcuInit*>("cuInit");
    CudaCompletionFunctions functions{library->symbol<tcuCtxPushCurrent_v2*>("cuCtxPushCurrent_v2"),
                                      library->symbol<tcuCtxPopCurrent_v2*>("cuCtxPopCurrent_v2"),
                                      library->symbol<tcuEventQuery*>("cuEventQuery")};
    if (init == nullptr || functions.context_push == nullptr || functions.context_pop == nullptr ||
        functions.event_query == nullptr) {
        error = "CUDA driver lacks required context/event functions";
        return MKVC_ERROR_NOT_SUPPORTED;
    }
    if (init(0) != CUDA_SUCCESS) {
        error = "CUDA driver initialization failed";
        return MKVC_ERROR_NOT_SUPPORTED;
    }
    completion = make_cuda_event_completion(
        reinterpret_cast<CUcontext>(static_cast<uintptr_t>(context)),
        reinterpret_cast<CUevent>(static_cast<uintptr_t>(event)), functions, std::move(library));
    return MKVC_OK;
}

mkvc_result cuda_stream_wait_event(uint64_t context, uint64_t event, uint64_t consumer_stream,
                                   std::string& error) {
    if (context == 0 || event == 0 || consumer_stream == 0) {
        error = "CUDA stream dependency requires context, event, and stream";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    auto library = load_cuda_driver(error);
    if (!library) return MKVC_ERROR_NOT_SUPPORTED;
    const auto init = library->symbol<tcuInit*>("cuInit");
    const auto context_push = library->symbol<tcuCtxPushCurrent_v2*>("cuCtxPushCurrent_v2");
    const auto context_pop = library->symbol<tcuCtxPopCurrent_v2*>("cuCtxPopCurrent_v2");
    const auto stream_wait = library->symbol<tcuStreamWaitEvent*>("cuStreamWaitEvent");
    if (init == nullptr || context_push == nullptr || context_pop == nullptr ||
        stream_wait == nullptr) {
        error = "CUDA driver lacks required stream/event functions";
        return MKVC_ERROR_NOT_SUPPORTED;
    }
    if (init(0) != CUDA_SUCCESS) {
        error = "CUDA driver initialization failed";
        return MKVC_ERROR_NOT_SUPPORTED;
    }
    auto cuda_context = reinterpret_cast<CUcontext>(static_cast<uintptr_t>(context));
    CudaContextGuard context_guard(cuda_context, context_push, context_pop);
    if (!context_guard) {
        error = "failed to activate CUDA context for stream dependency";
        return MKVC_ERROR_CODEC;
    }
    const CUresult waited =
        stream_wait(reinterpret_cast<CUstream>(static_cast<uintptr_t>(consumer_stream)),
                    reinterpret_cast<CUevent>(static_cast<uintptr_t>(event)), 0);
    if (!context_guard.release()) {
        error = "failed to release CUDA context after stream dependency";
        return MKVC_ERROR_CODEC;
    }
    if (waited != CUDA_SUCCESS) {
        error = "failed to insert CUDA stream event dependency";
        return MKVC_ERROR_CODEC;
    }
    return MKVC_OK;
}

}  // namespace mkvc::gpu::nvidia
