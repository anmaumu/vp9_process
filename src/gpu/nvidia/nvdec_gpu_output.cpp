#include "nvdec_gpu_output.hpp"

#include <utility>

#include "../gpu_frame_pool.hpp"
#include "nvdec_api.hpp"
#include "nvidia_native_handle.hpp"

namespace mkvc::gpu::nvidia {

mkvc_result acquire_nvdec_gpu_frame(NvdecApi& api, CUvideodecoder decoder, CUcontext context,
                                    unsigned long long device_pointer, unsigned int pitch,
                                    uint32_t width, uint32_t height, int64_t pts_ns,
                                    const std::shared_ptr<GpuFramePool>& pool,
                                    std::function<void()> release_mapping,
                                    std::shared_ptr<GpuFrameCore>& frame, std::string& error) {
    frame.reset();
    mkvc_gpu_frame_desc desc{};
    desc.struct_size = sizeof(desc);
    desc.struct_version = 1;
    desc.backend = MKVC_BACKEND_NVIDIA;
    desc.memory_type = MKVC_GPU_MEMORY_CUDA_POINTER;
    desc.device_id = 0;
    desc.pixel_format = MKVC_PIXEL_FORMAT_NV12;
    desc.width = width;
    desc.height = height;
    desc.plane_count = 2;
    desc.plane_offsets[1] = static_cast<uint64_t>(pitch) * height;
    desc.pitches[0] = pitch;
    desc.pitches[1] = pitch;
    desc.pts = pts_ns;

    mkvc_gpu_native_handle_desc native{};
    const mkvc_result native_result =
        make_cuda_handle(0, 0, MKVC_GPU_NATIVE_CUDA_POINTER, device_pointer,
                         reinterpret_cast<uintptr_t>(context), 0, 0, native, error);
    if (native_result != MKVC_OK) {
        (void)api.unmap_frame(decoder, device_pointer);
        return native_result;
    }

    auto ready = std::make_shared<ManualCompletion>();
    ready->complete();
    GpuFramePool::Acquisition acquisition;
    const mkvc_result acquired =
        pool->acquire(desc, ready, native, std::move(release_mapping), acquisition, error,
                      {BackendResourceKind::kNvidiaCudaFrame,
                       reinterpret_cast<void*>(static_cast<uintptr_t>(device_pointer))});
    if (acquired != MKVC_OK) {
        (void)api.unmap_frame(decoder, device_pointer);
        return acquired;
    }
    frame = std::move(acquisition.core);
    return MKVC_OK;
}

}  // namespace mkvc::gpu::nvidia
