/**
 * @file gpu_external_import_intel.cpp
 * @brief Intel D3D11, VA-API, and Level Zero external-frame C adapters.
 */
#include "gpu_external_import.hpp"

#include "intel/d3d11_completion.hpp"
#include "intel/level_zero_completion.hpp"
#include "intel/va_completion.hpp"

#include <exception>
#include <memory>
#include <string>
#include <utility>

extern thread_local std::string mkvc_last_error;

namespace external = mkvc::gpu::external;

extern "C" {

mkvc_result mkvc_gpu_frame_import_d3d11_fence(const mkvc_gpu_external_frame_config* config,
                                              mkvc_gpu_frame** out_frame) {
    mkvc_last_error.clear();
    if (out_frame != nullptr) *out_frame = nullptr;
    if (config == nullptr || out_frame == nullptr || config->struct_size < sizeof(*config) ||
        config->struct_version != 1) {
        return external::fail(MKVC_ERROR_INVALID_ARGUMENT,
                              "invalid D3D11 fence import configuration");
    }
    try {
        std::string error;
        if (!external::valid_layout(*config, error))
            return external::fail(MKVC_ERROR_INVALID_ARGUMENT, std::move(error));
        if (config->frame.backend != MKVC_BACKEND_INTEL ||
            config->frame.memory_type != MKVC_GPU_MEMORY_D3D11_TEXTURE ||
            config->native_handle.type != MKVC_GPU_NATIVE_D3D11_TEXTURE ||
            config->query != nullptr) {
            return external::fail(
                MKVC_ERROR_INVALID_ARGUMENT,
                "D3D11 fence import requires Intel texture and no producer callback");
        }
        std::shared_ptr<mkvc::gpu::Completion> producer;
        const auto result = mkvc::gpu::intel::load_d3d11_fence_completion(*config, producer, error);
        if (result != MKVC_OK) return external::fail(result, std::move(error));
        return external::import_with_completion(*config, std::move(producer), out_frame);
    } catch (const std::exception& exception) {
        return external::fail(MKVC_ERROR_INTERNAL, exception.what());
    } catch (...) {
        return external::fail(MKVC_ERROR_INTERNAL, "unknown D3D11 fence import failure");
    }
}

mkvc_result mkvc_gpu_frame_import_va_surface(const mkvc_gpu_external_frame_config* config,
                                             mkvc_gpu_frame** out_frame) {
    mkvc_last_error.clear();
    if (out_frame != nullptr) *out_frame = nullptr;
    if (config == nullptr || out_frame == nullptr || config->struct_size < sizeof(*config) ||
        config->struct_version != 1) {
        return external::fail(MKVC_ERROR_INVALID_ARGUMENT,
                              "invalid VA surface import configuration");
    }
    try {
        std::string error;
        if (!external::valid_layout(*config, error))
            return external::fail(MKVC_ERROR_INVALID_ARGUMENT, std::move(error));
        if (config->frame.backend != MKVC_BACKEND_INTEL ||
            config->frame.memory_type != MKVC_GPU_MEMORY_VA_SURFACE ||
            config->native_handle.type != MKVC_GPU_NATIVE_VA_SURFACE || config->query != nullptr) {
            return external::fail(
                MKVC_ERROR_INVALID_ARGUMENT,
                "native VA import requires Intel VA surface and no producer callback");
        }
        std::shared_ptr<mkvc::gpu::Completion> producer;
        const auto result = mkvc::gpu::intel::load_va_surface_completion(
            config->native_handle.handles[0], config->native_handle.handles[1], producer, error);
        if (result != MKVC_OK) return external::fail(result, std::move(error));
        return external::import_with_completion(*config, std::move(producer), out_frame);
    } catch (const std::exception& exception) {
        return external::fail(MKVC_ERROR_INTERNAL, exception.what());
    } catch (...) {
        return external::fail(MKVC_ERROR_INTERNAL, "unknown native VA surface import failure");
    }
}

mkvc_result mkvc_gpu_frame_import_level_zero_event(const mkvc_gpu_external_frame_config* config,
                                                   mkvc_gpu_frame** out_frame) {
    mkvc_last_error.clear();
    if (out_frame != nullptr) *out_frame = nullptr;
    if (config == nullptr || out_frame == nullptr || config->struct_size < sizeof(*config) ||
        config->struct_version != 1) {
        return external::fail(MKVC_ERROR_INVALID_ARGUMENT,
                              "invalid Level Zero event import configuration");
    }
    try {
        std::string error;
        if (!external::valid_layout(*config, error, true))
            return external::fail(MKVC_ERROR_INVALID_ARGUMENT, std::move(error));
        if (config->frame.backend != MKVC_BACKEND_INTEL ||
            config->frame.memory_type != MKVC_GPU_MEMORY_USM ||
            config->native_handle.type != MKVC_GPU_NATIVE_USM_POINTER ||
            config->native_handle.handles[3] == 0 || config->query != nullptr) {
            return external::fail(MKVC_ERROR_INVALID_ARGUMENT,
                                  "Level Zero event import requires Intel USM and no callback");
        }
        std::shared_ptr<mkvc::gpu::Completion> producer;
        const mkvc_result result = mkvc::gpu::intel::load_level_zero_event_completion(
            config->native_handle.handles[3], producer, error);
        if (result != MKVC_OK) return external::fail(result, std::move(error));
        return external::import_with_completion(*config, std::move(producer), out_frame);
    } catch (const std::exception& exception) {
        return external::fail(MKVC_ERROR_INTERNAL, exception.what());
    } catch (...) {
        return external::fail(MKVC_ERROR_INTERNAL, "unknown Level Zero event import failure");
    }
}

}  // extern "C"
