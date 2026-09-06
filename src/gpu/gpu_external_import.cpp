/**
 * @file gpu_external_import.cpp
 * @brief C ABI adapters for importing externally owned GPU frames.
 */
#include "gpu_frame.hpp"
#include "intel/d3d11_completion.hpp"
#include "intel/level_zero_completion.hpp"
#include "intel/va_completion.hpp"

#if defined(MKVC_HAS_NVIDIA)
#include "nvidia/cuda_completion.hpp"
#endif

#include <atomic>
#include <exception>
#include <limits>
#include <memory>
#include <string>
#include <utility>

extern thread_local std::string mkvc_last_error;

namespace {

mkvc_result gpu_fail(mkvc_result result, std::string message) {
    mkvc_last_error = std::move(message);
    return result;
}

/** Validate backend identity, resource type, layout, and completion contract. */
bool valid_external_layout(const mkvc_gpu_external_frame_config& config, std::string& error,
                           bool allow_usm_level_zero_event = false) {
    const auto& desc = config.frame;
    const auto& native = config.native_handle;
    if (desc.struct_size < sizeof(desc) || desc.struct_version != 1 ||
        native.struct_size < sizeof(native) || native.struct_version != 1) {
        error = "invalid external GPU descriptor version";
        return false;
    }
    if (config.release == nullptr) {
        error = "external GPU import requires a release callback";
        return false;
    }
    if (desc.width == 0 || desc.height == 0 || (desc.width & 1u) != 0 || (desc.height & 1u) != 0 ||
        desc.plane_count == 0 || desc.plane_count > 4 || desc.generation != native.generation ||
        desc.device_id != native.device_id || native.borrowed == 0) {
        error = "external GPU descriptor identity or dimensions are invalid";
        return false;
    }
    if (desc.backend == MKVC_BACKEND_NVIDIA) {
        const bool pointer = desc.memory_type == MKVC_GPU_MEMORY_CUDA_POINTER &&
                             native.type == MKVC_GPU_NATIVE_CUDA_POINTER;
        const bool array = desc.memory_type == MKVC_GPU_MEMORY_CUDA_ARRAY &&
                           native.type == MKVC_GPU_NATIVE_CUDA_ARRAY;
        if ((!pointer && !array) || native.handles[0] == 0 || native.handles[1] == 0 ||
            desc.pixel_format != MKVC_PIXEL_FORMAT_NV12 || desc.plane_count != 2 ||
            desc.pitches[0] < desc.width || desc.pitches[0] != desc.pitches[1] ||
            desc.pitches[0] > std::numeric_limits<uint32_t>::max() || desc.plane_offsets[0] != 0 ||
            desc.plane_offsets[1] != desc.pitches[0] * desc.height) {
            error = "external NVIDIA import requires CUDA NV12 pointer/array layout";
            return false;
        }
        if (array && desc.pitches[0] != desc.width) {
            error = "external CUDA-array NV12 pitch must equal its byte width";
            return false;
        }
        return true;
    }
    if (desc.backend == MKVC_BACKEND_INTEL) {
        const bool d3d11 = desc.memory_type == MKVC_GPU_MEMORY_D3D11_TEXTURE &&
                           native.type == MKVC_GPU_NATIVE_D3D11_TEXTURE;
        const bool va = desc.memory_type == MKVC_GPU_MEMORY_VA_SURFACE &&
                        native.type == MKVC_GPU_NATIVE_VA_SURFACE;
        const bool usm =
            desc.memory_type == MKVC_GPU_MEMORY_USM && native.type == MKVC_GPU_NATIVE_USM_POINTER;
        if ((!d3d11 && !va && !usm) || native.handles[0] == 0 ||
            desc.pixel_format != MKVC_PIXEL_FORMAT_NV12 || desc.plane_count != 2) {
            error = "external Intel import requires a D3D11, VA, or device-USM resource";
            return false;
        }
        if (usm &&
            (native.handles[1] == 0 || native.handles[2] == 0 || desc.pitches[0] < desc.width ||
             desc.pitches[0] != desc.pitches[1] ||
             desc.pitches[0] > std::numeric_limits<uint32_t>::max() || desc.plane_offsets[0] != 0 ||
             desc.plane_offsets[1] != desc.pitches[0] * desc.height || config.query != nullptr ||
             (native.handles[3] != 0 && !allow_usm_level_zero_event))) {
            error =
                "external Intel USM import requires linear NV12, context, queue, and valid "
                "completion identity";
            return false;
        }
        return true;
    }
    error = "external GPU import backend is unsupported";
    return false;
}

/** Build an owned lease while preserving the external producer's release rules. */
mkvc_result import_external_with_completion(const mkvc_gpu_external_frame_config& config,
                                            std::shared_ptr<mkvc::gpu::Completion> producer,
                                            mkvc_gpu_frame** out_frame) {
    try {
        const auto release = config.release;
        void* const user_data = config.user_data;
        auto accepted = std::make_shared<std::atomic<bool>>(false);
        auto recycle = [release, user_data, accepted](uint64_t) noexcept {
            if (!accepted->load(std::memory_order_acquire) || release == nullptr) return;
            try {
                release(user_data);
            } catch (...) {
            }
        };
        mkvc::gpu::BackendResource resource{};
        if (config.frame.backend == MKVC_BACKEND_NVIDIA) {
            resource.kind = mkvc::gpu::BackendResourceKind::kNvidiaCudaFrame;
            resource.object =
                reinterpret_cast<void*>(static_cast<uintptr_t>(config.native_handle.handles[0]));
        }
        auto core = std::make_shared<mkvc::gpu::GpuFrameCore>(
            config.frame, std::move(producer), std::move(recycle), config.native_handle, resource);
        *out_frame = mkvc::gpu::make_handle(core);
        if (*out_frame == nullptr) {
            return gpu_fail(MKVC_ERROR_INTERNAL, "failed to allocate external GPU frame handle");
        }
        accepted->store(true, std::memory_order_release);
        return MKVC_OK;
    } catch (const std::exception& exception) {
        return gpu_fail(MKVC_ERROR_INTERNAL, exception.what());
    } catch (...) {
        return gpu_fail(MKVC_ERROR_INTERNAL, "unknown external GPU frame import failure");
    }
}

}  // namespace

extern "C" {

mkvc_result mkvc_gpu_frame_import_external(const mkvc_gpu_external_frame_config* config,
                                           mkvc_gpu_frame** out_frame) {
    mkvc_last_error.clear();
    if (config == nullptr || out_frame == nullptr || config->struct_size < sizeof(*config) ||
        config->struct_version != 1) {
        return gpu_fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid external GPU frame configuration");
    }
    *out_frame = nullptr;
    std::string error;
    if (!valid_external_layout(*config, error)) {
        return gpu_fail(MKVC_ERROR_INVALID_ARGUMENT, std::move(error));
    }
    try {
        std::shared_ptr<mkvc::gpu::Completion> producer;
        if (config->query == nullptr) {
            auto complete = std::make_shared<mkvc::gpu::ManualCompletion>();
            complete->complete();
            producer = std::move(complete);
        } else {
            const auto query = config->query;
            void* const user_data = config->user_data;
            producer = std::make_shared<mkvc::gpu::CallbackCompletion>(
                [query, user_data](bool& complete, std::string& callback_error) {
                    uint32_t value = 0;
                    try {
                        const mkvc_result result = query(user_data, &value);
                        if (result != MKVC_OK) {
                            callback_error = "external GPU producer query failed";
                            return result;
                        }
                    } catch (...) {
                        callback_error = "external GPU producer query threw";
                        return MKVC_ERROR_INTERNAL;
                    }
                    complete = value != 0;
                    return MKVC_OK;
                });
        }
        return import_external_with_completion(*config, std::move(producer), out_frame);
    } catch (const std::exception& exception) {
        return gpu_fail(MKVC_ERROR_INTERNAL, exception.what());
    } catch (...) {
        return gpu_fail(MKVC_ERROR_INTERNAL, "unknown external GPU frame import failure");
    }
}

mkvc_result mkvc_gpu_frame_import_d3d11_fence(const mkvc_gpu_external_frame_config* config,
                                              mkvc_gpu_frame** out_frame) {
    mkvc_last_error.clear();
    if (out_frame != nullptr) *out_frame = nullptr;
    if (config == nullptr || out_frame == nullptr || config->struct_size < sizeof(*config) ||
        config->struct_version != 1) {
        return gpu_fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid D3D11 fence import configuration");
    }
    try {
        std::string error;
        if (!valid_external_layout(*config, error))
            return gpu_fail(MKVC_ERROR_INVALID_ARGUMENT, std::move(error));
        if (config->frame.backend != MKVC_BACKEND_INTEL ||
            config->frame.memory_type != MKVC_GPU_MEMORY_D3D11_TEXTURE ||
            config->native_handle.type != MKVC_GPU_NATIVE_D3D11_TEXTURE ||
            config->query != nullptr) {
            return gpu_fail(MKVC_ERROR_INVALID_ARGUMENT,
                            "D3D11 fence import requires Intel texture and no producer callback");
        }
        std::shared_ptr<mkvc::gpu::Completion> producer;
        const auto result = mkvc::gpu::intel::load_d3d11_fence_completion(*config, producer, error);
        if (result != MKVC_OK) return gpu_fail(result, std::move(error));
        return import_external_with_completion(*config, std::move(producer), out_frame);
    } catch (const std::exception& exception) {
        return gpu_fail(MKVC_ERROR_INTERNAL, exception.what());
    } catch (...) {
        return gpu_fail(MKVC_ERROR_INTERNAL, "unknown D3D11 fence import failure");
    }
}

mkvc_result mkvc_gpu_frame_import_va_surface(const mkvc_gpu_external_frame_config* config,
                                             mkvc_gpu_frame** out_frame) {
    mkvc_last_error.clear();
    if (out_frame != nullptr) *out_frame = nullptr;
    if (config == nullptr || out_frame == nullptr || config->struct_size < sizeof(*config) ||
        config->struct_version != 1) {
        return gpu_fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid VA surface import configuration");
    }
    try {
        std::string error;
        if (!valid_external_layout(*config, error))
            return gpu_fail(MKVC_ERROR_INVALID_ARGUMENT, std::move(error));
        if (config->frame.backend != MKVC_BACKEND_INTEL ||
            config->frame.memory_type != MKVC_GPU_MEMORY_VA_SURFACE ||
            config->native_handle.type != MKVC_GPU_NATIVE_VA_SURFACE || config->query != nullptr) {
            return gpu_fail(MKVC_ERROR_INVALID_ARGUMENT,
                            "native VA import requires Intel VA surface and no producer callback");
        }
        std::shared_ptr<mkvc::gpu::Completion> producer;
        const auto result = mkvc::gpu::intel::load_va_surface_completion(
            config->native_handle.handles[0], config->native_handle.handles[1], producer, error);
        if (result != MKVC_OK) return gpu_fail(result, std::move(error));
        return import_external_with_completion(*config, std::move(producer), out_frame);
    } catch (const std::exception& exception) {
        return gpu_fail(MKVC_ERROR_INTERNAL, exception.what());
    } catch (...) {
        return gpu_fail(MKVC_ERROR_INTERNAL, "unknown native VA surface import failure");
    }
}

mkvc_result mkvc_gpu_frame_import_cuda_event(const mkvc_gpu_external_frame_config* config,
                                             mkvc_gpu_frame** out_frame) {
    mkvc_last_error.clear();
    if (config == nullptr || out_frame == nullptr || config->struct_size < sizeof(*config) ||
        config->struct_version != 1) {
        return gpu_fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid CUDA event frame configuration");
    }
    *out_frame = nullptr;
    std::string error;
    if (!valid_external_layout(*config, error))
        return gpu_fail(MKVC_ERROR_INVALID_ARGUMENT, std::move(error));
    if (config->frame.backend != MKVC_BACKEND_NVIDIA || config->query != nullptr ||
        config->native_handle.handles[3] == 0) {
        return gpu_fail(MKVC_ERROR_INVALID_ARGUMENT,
                        "CUDA event import requires NVIDIA, a native event, and no query callback");
    }
#if defined(MKVC_HAS_NVIDIA)
    std::shared_ptr<mkvc::gpu::Completion> producer;
    const mkvc_result result = mkvc::gpu::nvidia::load_cuda_event_completion(
        config->native_handle.handles[1], config->native_handle.handles[3], producer, error);
    if (result != MKVC_OK) return gpu_fail(result, std::move(error));
    return import_external_with_completion(*config, std::move(producer), out_frame);
#else
    return gpu_fail(MKVC_ERROR_NOT_SUPPORTED, "CUDA event import was not enabled in this build");
#endif
}

mkvc_result mkvc_gpu_frame_import_level_zero_event(const mkvc_gpu_external_frame_config* config,
                                                   mkvc_gpu_frame** out_frame) {
    mkvc_last_error.clear();
    if (out_frame != nullptr) *out_frame = nullptr;
    if (config == nullptr || out_frame == nullptr || config->struct_size < sizeof(*config) ||
        config->struct_version != 1) {
        return gpu_fail(MKVC_ERROR_INVALID_ARGUMENT,
                        "invalid Level Zero event import configuration");
    }
    try {
        std::string error;
        if (!valid_external_layout(*config, error, true))
            return gpu_fail(MKVC_ERROR_INVALID_ARGUMENT, std::move(error));
        if (config->frame.backend != MKVC_BACKEND_INTEL ||
            config->frame.memory_type != MKVC_GPU_MEMORY_USM ||
            config->native_handle.type != MKVC_GPU_NATIVE_USM_POINTER ||
            config->native_handle.handles[3] == 0 || config->query != nullptr) {
            return gpu_fail(MKVC_ERROR_INVALID_ARGUMENT,
                            "Level Zero event import requires Intel USM and no callback");
        }
        std::shared_ptr<mkvc::gpu::Completion> producer;
        const mkvc_result result = mkvc::gpu::intel::load_level_zero_event_completion(
            config->native_handle.handles[3], producer, error);
        if (result != MKVC_OK) return gpu_fail(result, std::move(error));
        return import_external_with_completion(*config, std::move(producer), out_frame);
    } catch (const std::exception& exception) {
        return gpu_fail(MKVC_ERROR_INTERNAL, exception.what());
    } catch (...) {
        return gpu_fail(MKVC_ERROR_INTERNAL, "unknown Level Zero event import failure");
    }
}

}  // extern "C"
