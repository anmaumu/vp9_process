#include "level_zero_completion.hpp"

#include <limits>
#include <mutex>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace mkvc::gpu::intel {
namespace {
constexpr uint32_t kZeSuccess = 0;
constexpr uint32_t kZeNotReady = 1;
}

std::shared_ptr<Completion> make_level_zero_event_completion(
    void* event, ZeEventQueryStatus query,
    std::shared_ptr<void> library_keepalive) {
    struct State {
        std::mutex mutex;
        bool terminal = false;
        mkvc_result result = MKVC_OK;
        std::string error;
    };
    auto state = std::make_shared<State>();
    return std::make_shared<CallbackCompletion>(
        [event, query, state, keepalive = std::move(library_keepalive)](
            bool& complete, std::string& error) {
            (void)keepalive;
            std::lock_guard<std::mutex> lock(state->mutex);
            if (!state->terminal) {
                if (event == nullptr || query == nullptr) {
                    state->result = MKVC_ERROR_INVALID_ARGUMENT;
                    state->error = "invalid Level Zero event synchronization arguments";
                } else {
                    const uint32_t status = query(event);
                    if (status == kZeNotReady) {
                        complete = false;
                        return MKVC_OK;
                    }
                    if (status != kZeSuccess) {
                        state->result = MKVC_ERROR_CODEC;
                        state->error = "zeEventQueryStatus failed with status " +
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

mkvc_result load_level_zero_event_completion(
    uint64_t event, std::shared_ptr<Completion>& completion,
    std::string& error) {
    completion.reset();
    if (event == 0 || event > (std::numeric_limits<uintptr_t>::max)()) {
        error = "Level Zero synchronization requires a valid event handle";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
#if defined(_WIN32)
    auto library = std::shared_ptr<void>(
        LoadLibraryW(L"ze_loader.dll"),
        [](void* handle) { if (handle) FreeLibrary(static_cast<HMODULE>(handle)); });
    const auto query = library ? reinterpret_cast<ZeEventQueryStatus>(
        GetProcAddress(static_cast<HMODULE>(library.get()), "zeEventQueryStatus")) : nullptr;
#else
    auto library = std::shared_ptr<void>(
        dlopen("libze_loader.so.1", RTLD_NOW | RTLD_LOCAL),
        [](void* handle) { if (handle) dlclose(handle); });
    const auto query = library ? reinterpret_cast<ZeEventQueryStatus>(
        dlsym(library.get(), "zeEventQueryStatus")) : nullptr;
#endif
    if (!library || query == nullptr) {
        error = "Level Zero loader or zeEventQueryStatus is unavailable";
        return MKVC_ERROR_NOT_SUPPORTED;
    }
    auto candidate = make_level_zero_event_completion(
        reinterpret_cast<void*>(static_cast<uintptr_t>(event)), query,
        std::move(library));
    const mkvc_result result = candidate->wait(0, error);
    if (result != MKVC_OK && result != MKVC_ERROR_TIMEOUT) return result;
    completion = std::move(candidate);
    return MKVC_OK;
}

}  // namespace mkvc::gpu::intel
