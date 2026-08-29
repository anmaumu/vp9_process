#include "gpu_frame.hpp"
#include "gpu_frame_pool.hpp"
#include "intel_native_handle.hpp"
#include "nvidia_native_handle.hpp"

#include <cassert>
#include <memory>
#include <string>
#include <atomic>
#include <thread>

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
    mkvc_gpu_native_handle_desc native{};
    native.struct_size = sizeof(native);
    native.struct_version = 1;
    native.type = MKVC_GPU_NATIVE_CUDA_POINTER;
    native.borrowed = 1;
    native.device_id = desc.device_id;
    native.generation = desc.generation;
    native.handles[0] = 0x12340000;
    native.handles[1] = 0x56780000;

    auto producer = std::make_shared<mkvc::gpu::ManualCompletion>();
    auto consumer = std::make_shared<mkvc::gpu::ManualCompletion>();
    unsigned recycled = 0;
    uint64_t recycled_generation = 0;
    auto core = std::make_shared<mkvc::gpu::GpuFrameCore>(
        desc, producer, [&](uint64_t generation) {
            ++recycled;
            recycled_generation = generation;
        }, native);
    std::string error;
    assert(core->add_consumer(consumer, error) == MKVC_OK);
    mkvc_gpu_frame* handle = mkvc::gpu::make_handle(core);
    assert(handle != nullptr && core->external_leases() == 1);

    mkvc_gpu_frame_desc copied{};
    copied.struct_size = sizeof(copied);
    copied.struct_version = 1;
    assert(mkvc_gpu_frame_get_desc(handle, &copied) == MKVC_OK);
    assert(copied.generation == 42 && copied.pitches[0] == 2048);
    mkvc_gpu_native_handle_desc exported{};
    exported.struct_size = sizeof(exported);
    exported.struct_version = 1;
    assert(mkvc_gpu_frame_get_native_handle(handle, &exported) == MKVC_OK);
    assert(exported.borrowed == 1 && exported.handles[0] == 0x12340000);

    mkvc_gpu_native_handle_desc platform{};
    assert(mkvc::gpu::intel::make_d3d11_handle(
        1, 2, 0x1000, 3, platform, error) == MKVC_OK);
    assert(platform.type == MKVC_GPU_NATIVE_D3D11_TEXTURE && platform.handles[1] == 3);
    assert(mkvc::gpu::intel::make_va_surface_handle(
        1, 2, 0x2000, 17, platform, error) == MKVC_OK);
    assert(platform.type == MKVC_GPU_NATIVE_VA_SURFACE && platform.handles[1] == 17);
    assert(mkvc::gpu::nvidia::make_cuda_handle(
        1, 2, MKVC_GPU_NATIVE_CUDA_ARRAY, 0x3000, 0x4000, 0x5000,
        0x6000, platform, error) == MKVC_OK);
    assert(platform.handles[0] == 0x3000 && platform.handles[3] == 0x6000);
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

    std::atomic<unsigned> polls{0};
    mkvc::gpu::CallbackCompletion callback(
        [&polls](bool& complete, std::string&) {
            complete = ++polls >= 3;
            return MKVC_OK;
        });
    assert(callback.query(error) == MKVC_GPU_COMPLETION_PENDING);
    assert(callback.wait(50, error) == MKVC_OK);
    assert(polls >= 3);
    mkvc::gpu::CallbackCompletion never(
        [](bool& complete, std::string&) { complete = false; return MKVC_OK; });
    assert(never.wait(0, error) == MKVC_ERROR_TIMEOUT);

    auto pool = std::make_shared<mkvc::gpu::GpuFramePool>(2);
    auto pool_done_a = std::make_shared<mkvc::gpu::ManualCompletion>();
    auto pool_done_b = std::make_shared<mkvc::gpu::ManualCompletion>();
    mkvc::gpu::GpuFramePool::Acquisition acquired_a;
    mkvc::gpu::GpuFramePool::Acquisition acquired_b;
    mkvc::gpu::GpuFramePool::Acquisition blocked;
    unsigned resources_released = 0;
    assert(pool->acquire(desc, pool_done_a, native, [&] { ++resources_released; },
                         acquired_a, error) == MKVC_OK);
    assert(pool->acquire(desc, pool_done_b, native, [&] { ++resources_released; },
                         acquired_b, error) == MKVC_OK);
    assert(pool->in_use() == 2 && pool->peak_in_use() == 2);
    assert(pool->acquire(desc, std::make_shared<mkvc::gpu::ManualCompletion>(),
                         native, {}, blocked, error) == MKVC_WOULD_BLOCK);
    mkvc_gpu_frame* pooled_handle = mkvc::gpu::make_handle(acquired_a.core);
    pool_done_a->complete();
    acquired_a.core->poll_recycle();
    assert(pool->in_use() == 2);  // external lease still owns slot
    mkvc_gpu_frame_release(pooled_handle);
    assert(pool->in_use() == 1);
    const uint64_t first_generation = acquired_a.generation;
    acquired_a.core.reset();
    auto pool_done_c = std::make_shared<mkvc::gpu::ManualCompletion>();
    assert(pool->acquire(desc, pool_done_c, native, [&] { ++resources_released; },
                         blocked, error) == MKVC_OK);
    assert(blocked.generation > first_generation);
    pool_done_b->complete();
    acquired_b.core->poll_recycle();
    pool_done_c->complete();
    blocked.core->poll_recycle();
    assert(pool->in_use() == 0 && pool->peak_in_use() == 2);
    assert(resources_released == 3);

    unsigned abandoned_releases = 0;
    auto abandoned_done = std::make_shared<mkvc::gpu::ManualCompletion>();
    mkvc::gpu::GpuFramePool::Acquisition abandoned;
    assert(pool->acquire(desc, abandoned_done, native,
                         [&] { ++abandoned_releases; }, abandoned, error) == MKVC_OK);
    std::thread complete_abandoned([abandoned_done] { abandoned_done->complete(); });
    abandoned.core.reset();  // destructor waits and releases backend resource
    complete_abandoned.join();
    assert(abandoned_releases == 1 && pool->in_use() == 0);
    return 0;
}
