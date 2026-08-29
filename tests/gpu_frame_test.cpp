#include "gpu_frame.hpp"

#include <cassert>
#include <memory>
#include <string>

thread_local std::string mkvc_last_error;

int main() {
    mkvc_gpu_frame_desc desc{};
    desc.struct_size = sizeof(desc);
    desc.struct_version = 1;
    desc.backend = MKVC_BACKEND_NVIDIA;
    desc.memory_type = MKVC_GPU_MEMORY_CUDA_POINTER;
    desc.device_id = 7;
    desc.generation = 42;
    desc.pixel_format = MKVC_PIXEL_FORMAT_NV12;
    desc.width = 1920;
    desc.height = 1080;
    desc.plane_count = 2;
    desc.pitches[0] = desc.pitches[1] = 2048;
    desc.plane_offsets[1] = 2048 * 1080;
    desc.pts = 123;

    auto producer = std::make_shared<mkvc::gpu::ManualCompletion>();
    auto consumer = std::make_shared<mkvc::gpu::ManualCompletion>();
    unsigned recycled = 0;
    uint64_t recycled_generation = 0;
    auto core = std::make_shared<mkvc::gpu::GpuFrameCore>(
        desc, producer, [&](uint64_t generation) {
            ++recycled;
            recycled_generation = generation;
        });
    std::string error;
    assert(core->add_consumer(consumer, error) == MKVC_OK);
    mkvc_gpu_frame* handle = mkvc::gpu::make_handle(core);
    assert(handle != nullptr && core->external_leases() == 1);

    mkvc_gpu_frame_desc copied{};
    copied.struct_size = sizeof(copied);
    copied.struct_version = 1;
    assert(mkvc_gpu_frame_get_desc(handle, &copied) == MKVC_OK);
    assert(copied.generation == 42 && copied.pitches[0] == 2048);
    uint32_t status = 99;
    assert(mkvc_gpu_frame_query_completion(handle, &status) == MKVC_OK);
    assert(status == MKVC_GPU_COMPLETION_PENDING);
    assert(mkvc_gpu_frame_wait(handle, 0) == MKVC_ERROR_TIMEOUT);

    assert(mkvc_gpu_frame_retain(handle) == MKVC_OK);
    mkvc_gpu_frame_release(handle);
    assert(core->external_leases() == 1 && recycled == 0);
    producer->complete();
    assert(mkvc_gpu_frame_wait(handle, 10) == MKVC_OK);
    mkvc_gpu_frame_release(handle);
    assert(core->external_leases() == 0 && recycled == 0);
    consumer->complete();
    core->poll_recycle();
    assert(recycled == 1 && recycled_generation == 42 && core->recycled());
    core->poll_recycle();
    assert(recycled == 1);

    auto failed = std::make_shared<mkvc::gpu::ManualCompletion>();
    auto failed_core = std::make_shared<mkvc::gpu::GpuFrameCore>(
        desc, failed, mkvc::gpu::GpuFrameCore::RecycleCallback{});
    mkvc_gpu_frame* failed_handle = mkvc::gpu::make_handle(failed_core);
    failed->fail("injected device loss");
    assert(mkvc_gpu_frame_wait(failed_handle, 10) == MKVC_ERROR_CODEC);
    assert(mkvc_last_error.find("device loss") != std::string::npos);
    mkvc_gpu_frame_release(failed_handle);
    assert(failed_core->recycled());

    assert(mkvc_gpu_frame_retain(nullptr) == MKVC_ERROR_INVALID_STATE);
    mkvc_gpu_frame_release(nullptr);
    return 0;
}
