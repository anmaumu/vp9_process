#include "d3d11_completion.hpp"

#include <limits>
#include <mutex>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11_4.h>
#include <wrl/client.h>
#endif

namespace mkvc::gpu::intel {
std::shared_ptr<Completion> make_d3d11_fence_completion(
    uint64_t target, std::function<uint64_t()> query) {
    struct State {
        std::mutex mutex;
        bool terminal = false;
        mkvc_result result = MKVC_OK;
        std::string error;
    };
    auto state = std::make_shared<State>();
    return std::make_shared<CallbackCompletion>(
        [target, query = std::move(query), state](bool& complete, std::string& error) {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (!state->terminal) {
                if (!query || target == 0 || target == UINT64_MAX) {
                    state->result = MKVC_ERROR_INVALID_ARGUMENT;
                    state->error = "invalid D3D11 fence target or query";
                } else {
                    const uint64_t value = query();
                    if (value == UINT64_MAX) {
                        state->result = MKVC_ERROR_CODEC;
                        state->error = "D3D11 fence device was removed";
                    } else if (value < target) {
                        complete = false;
                        return MKVC_OK;
                    }
                }
                state->terminal = true;
            }
            complete = state->result == MKVC_OK;
            error = state->error;
            return state->result;
        });
}

mkvc_result load_d3d11_fence_completion(
    const mkvc_gpu_external_frame_config& config,
    std::shared_ptr<Completion>& completion, std::string& error) {
    completion.reset();
    const auto& handles = config.native_handle.handles;
    if (handles[0] == 0 || handles[0] > std::numeric_limits<uintptr_t>::max() ||
        handles[1] != 0 || handles[2] == 0 ||
        handles[2] > std::numeric_limits<uintptr_t>::max() ||
        handles[3] == 0 || handles[3] == UINT64_MAX) {
        error = "D3D11 import requires texture, subresource zero, fence and target 1..UINT64_MAX-1";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
#ifdef _WIN32
    using Microsoft::WRL::ComPtr;
    // Borrowed process-local interface pointers must be valid on entry.
    ComPtr<ID3D11Texture2D> texture = reinterpret_cast<ID3D11Texture2D*>(
        static_cast<uintptr_t>(handles[0]));
    ComPtr<ID3D11Fence> fence = reinterpret_cast<ID3D11Fence*>(
        static_cast<uintptr_t>(handles[2]));
    ComPtr<ID3D11Device> texture_device, fence_device;
    texture->GetDevice(&texture_device);
    fence->GetDevice(&fence_device);
    ComPtr<IUnknown> texture_identity, fence_identity;
    if (!texture_device || !fence_device ||
        FAILED(texture_device.As(&texture_identity)) ||
        FAILED(fence_device.As(&fence_identity)) ||
        texture_identity.Get() != fence_identity.Get()) {
        error = "D3D11 texture and fence must belong to the same device";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    if (desc.Format != DXGI_FORMAT_NV12 || desc.Width != config.frame.width ||
        desc.Height != config.frame.height || desc.MipLevels != 1 ||
        desc.ArraySize != 1 || desc.SampleDesc.Count != 1 ||
        desc.Usage != D3D11_USAGE_DEFAULT || desc.CPUAccessFlags != 0) {
        error = "D3D11 import requires a matching GPU-only single-subresource NV12 texture";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    // Poll only: no context Flush/Map/CopyResource or device-wide wait here.
    // https://learn.microsoft.com/windows/win32/api/d3d11_3/nf-d3d11_3-id3d11fence-getcompletedvalue
    auto candidate = make_d3d11_fence_completion(handles[3],
        [texture, fence, texture_device]() -> uint64_t {
            (void)texture;  // Keep the imported resource alive through completion.
            if (FAILED(texture_device->GetDeviceRemovedReason())) return UINT64_MAX;
            return fence->GetCompletedValue();
        });
    const auto result = candidate->wait(0, error);
    if (result != MKVC_OK && result != MKVC_ERROR_TIMEOUT) return result;
    completion = std::move(candidate);
    return MKVC_OK;
#else
    error = "native D3D11 fence import requires Windows";
    return MKVC_ERROR_NOT_SUPPORTED;
#endif
}
}  // namespace mkvc::gpu::intel
