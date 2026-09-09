/**
 * @file gpu_external_import.cpp
 * @brief C ABI adapters for importing externally owned GPU frames.
 */
#include "gpu_external_import.hpp"

#include <atomic>
#include <exception>
#include <limits>
#include <memory>
#include <string>
#include <utility>

extern thread_local std::string mkvc_last_error;

namespace mkvc::gpu::external {

mkvc_result fail(const mkvc_result result, std::string message) {
    mkvc_last_error = std::move(message);
    return result;
}

/** Validate backend identity, resource type, layout, and completion contract. */
bool valid_layout(const mkvc_gpu_external_frame_config& config, std::string& error,
                  const bool allow_usm_level_zero_event) {
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
mkvc_result import_with_completion(const mkvc_gpu_external_frame_config& config,
                                   std::shared_ptr<Completion> producer,
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
            return fail(MKVC_ERROR_INTERNAL, "failed to allocate external GPU frame handle");
        }
        accepted->store(true, std::memory_order_release);
        return MKVC_OK;
    } catch (const std::exception& exception) {
        return fail(MKVC_ERROR_INTERNAL, exception.what());
    } catch (...) {
        return fail(MKVC_ERROR_INTERNAL, "unknown external GPU frame import failure");
    }
}

}  // namespace mkvc::gpu::external

extern "C" {

mkvc_result mkvc_gpu_frame_import_external(const mkvc_gpu_external_frame_config* config,
                                           mkvc_gpu_frame** out_frame) {
    mkvc_last_error.clear();
    if (config == nullptr || out_frame == nullptr || config->struct_size < sizeof(*config) ||
        config->struct_version != 1) {
        return mkvc::gpu::external::fail(MKVC_ERROR_INVALID_ARGUMENT,
                                         "invalid external GPU frame configuration");
    }
    *out_frame = nullptr;
    std::string error;
    if (!mkvc::gpu::external::valid_layout(*config, error)) {
        return mkvc::gpu::external::fail(MKVC_ERROR_INVALID_ARGUMENT, std::move(error));
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
        return mkvc::gpu::external::import_with_completion(*config, std::move(producer), out_frame);
    } catch (const std::exception& exception) {
        return mkvc::gpu::external::fail(MKVC_ERROR_INTERNAL, exception.what());
    } catch (...) {
        return mkvc::gpu::external::fail(MKVC_ERROR_INTERNAL,
                                         "unknown external GPU frame import failure");
    }
}

}  // extern "C"
