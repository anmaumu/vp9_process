#include "gpu_frame.hpp"

#include <atomic>
#include <cassert>
#include <memory>
#include <string>
#include <thread>

#include "d3d11_completion.hpp"
#include "gpu_frame_pool.hpp"
#include "intel_native_handle.hpp"
#include "level_zero_completion.hpp"
#include "nvenc_gpu_frame_validation.hpp"
#include "nvidia_native_handle.hpp"
#include "va_completion.hpp"

thread_local std::string mkvc_last_error;

namespace {
struct VaProbe {
    int result = mkvc::gpu::intel::kVaTimedOut;
    unsigned calls = 0;
};
int fake_va_sync(void* display, unsigned int surface, uint64_t timeout_ns) {
    assert(surface == 0 && timeout_ns == 0);
    auto& probe = *static_cast<VaProbe*>(display);
    ++probe.calls;
    return probe.result;
}
struct TestDLDevice {
    int32_t type;
    int32_t id;
};
struct TestDLDataType {
    uint8_t code;
    uint8_t bits;
    uint16_t lanes;
};
struct TestDLTensor {
    void* data;
    TestDLDevice device;
    int32_t ndim;
    TestDLDataType dtype;
    int64_t* shape;
    int64_t* strides;
    uint64_t byte_offset;
};
struct TestDLManagedTensor {
    TestDLTensor dl_tensor;
    void* manager_ctx;
    void (*deleter)(TestDLManagedTensor*);
};
struct ExternalState {
    std::atomic<bool> complete{false};
    std::atomic<unsigned> releases{0};
};
mkvc_result query_external(void* opaque, uint32_t* complete) {
    auto* state = static_cast<ExternalState*>(opaque);
    if (state == nullptr || complete == nullptr) return MKVC_ERROR_INVALID_ARGUMENT;
    *complete = state->complete.load() ? 1u : 0u;
    return MKVC_OK;
}
void release_external(void* opaque) { static_cast<ExternalState*>(opaque)->releases.fetch_add(1); }
uint32_t fake_ze_event_query(void* event) { return *static_cast<uint32_t*>(event); }
struct DependencyProbe {
    uint64_t event = 0;
    uint64_t stream = 0;
    unsigned calls = 0;
};
mkvc_result register_dependency(void* opaque, uint64_t event, uint64_t stream) {
    auto& probe = *static_cast<DependencyProbe*>(opaque);
    probe.event = event;
    probe.stream = stream;
    ++probe.calls;
    return MKVC_OK;
}
mkvc_result reject_dependency(void*, uint64_t, uint64_t) { return MKVC_ERROR_NOT_SUPPORTED; }
}  // namespace

int main() {
    {
        uint32_t status = 1;  // ZE_RESULT_NOT_READY
        std::string error;
        auto completion =
            mkvc::gpu::intel::make_level_zero_event_completion(&status, fake_ze_event_query);
        assert(completion->wait(0, error) == MKVC_ERROR_TIMEOUT);
        status = 0;
        assert(completion->wait(20, error) == MKVC_OK);
        status = 0x70000001;
        assert(completion->query(error) == MKVC_GPU_COMPLETION_COMPLETE);
        completion =
            mkvc::gpu::intel::make_level_zero_event_completion(&status, fake_ze_event_query);
        assert(completion->wait(0, error) == MKVC_ERROR_CODEC);
        assert(completion->query(error) == MKVC_GPU_COMPLETION_FAILED);
        assert(mkvc::gpu::intel::make_level_zero_event_completion(nullptr, fake_ze_event_query)
                   ->wait(0, error) == MKVC_ERROR_INVALID_ARGUMENT);
    }
    {
        using mkvc::gpu::intel::make_d3d11_fence_completion;
        uint64_t value = 0;
        unsigned calls = 0;
        auto query = [&] {
            ++calls;
            return value;
        };
        auto completion = make_d3d11_fence_completion(7, query);
        std::string error;
        assert(completion->wait(0, error) == MKVC_ERROR_TIMEOUT);
        value = 6;
        assert(completion->query(error) == MKVC_GPU_COMPLETION_PENDING);
        value = 8;  // Fence may advance beyond the requested value.
        assert(completion->wait(20, error) == MKVC_OK);
        const auto completed_calls = calls;
        value = UINT64_MAX;
        assert(completion->query(error) == MKVC_GPU_COMPLETION_COMPLETE);
        assert(calls == completed_calls);
        completion = make_d3d11_fence_completion(7, query);
        assert(completion->wait(0, error) == MKVC_ERROR_CODEC);
        value = 8;
        assert(completion->query(error) == MKVC_GPU_COMPLETION_FAILED);
        assert(make_d3d11_fence_completion(0, query)->wait(0, error) ==
               MKVC_ERROR_INVALID_ARGUMENT);
        assert(make_d3d11_fence_completion(UINT64_MAX, query)->wait(0, error) ==
               MKVC_ERROR_INVALID_ARGUMENT);
        assert(make_d3d11_fence_completion(1, {})->wait(0, error) == MKVC_ERROR_INVALID_ARGUMENT);
    }
    {
        using namespace mkvc::gpu::intel;
        VaProbe probe;
        std::string error;
        auto owner = std::make_shared<int>(1);
        std::weak_ptr<int> weak_owner = owner;
        auto completion = make_va_surface_completion(&probe, 0, fake_va_sync, owner);
        owner.reset();
        assert(!weak_owner.expired());
        assert(completion->wait(0, error) == MKVC_ERROR_TIMEOUT);
        assert(completion->query(error) == MKVC_GPU_COMPLETION_PENDING);
        probe.result = kVaSuccess;
        assert(completion->wait(10, error) == MKVC_OK);
        const auto calls = probe.calls;
        probe.result = 1;
        assert(completion->query(error) == MKVC_GPU_COMPLETION_COMPLETE);
        assert(probe.calls == calls);  // Do not wait on later consumer work.
        completion.reset();
        assert(weak_owner.expired());
        probe.result = kVaUnimplemented;
        completion = make_va_surface_completion(&probe, 0, fake_va_sync);
        assert(completion->wait(0, error) == MKVC_ERROR_NOT_SUPPORTED);
        probe.result = kVaSuccess;
        assert(completion->query(error) == MKVC_GPU_COMPLETION_FAILED);
        completion = make_va_surface_completion(&probe, UINT32_MAX, fake_va_sync);
        assert(completion->wait(0, error) == MKVC_ERROR_INVALID_ARGUMENT);
        completion = make_va_surface_completion(&probe, 0, nullptr);
        assert(completion->wait(0, error) == MKVC_ERROR_INVALID_ARGUMENT);
        probe.result = 1;
        completion = make_va_surface_completion(&probe, 0, fake_va_sync);
        assert(completion->wait(0, error) == MKVC_ERROR_CODEC);
    }
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
        desc, producer,
        [&](uint64_t generation) {
            ++recycled;
            recycled_generation = generation;
        },
        native);
    std::string error;

    auto nvenc_ready = std::make_shared<mkvc::gpu::ManualCompletion>();
    nvenc_ready->complete();
    auto nvenc_core = std::make_shared<mkvc::gpu::GpuFrameCore>(
        desc, nvenc_ready, [](uint64_t) {}, native,
        mkvc::gpu::BackendResource{mkvc::gpu::BackendResourceKind::kNvidiaCudaFrame,
                                   reinterpret_cast<void*>(native.handles[0])});
    mkvc::gpu::nvidia::NvencCudaFrameView nvenc_view;
    assert(mkvc::gpu::nvidia::prepare_nvenc_cuda_frame(nvenc_core, 1920, 1080, nvenc_view, error) ==
           MKVC_OK);
    assert(!nvenc_view.cuda_array && nvenc_view.resource_handle == native.handles[0] &&
           nvenc_view.context_handle == native.handles[1] && nvenc_view.pitch == 2048 &&
           nvenc_view.pts_ns == 123);
    auto invalid_nvenc_desc = desc;
    invalid_nvenc_desc.pitches[1] = 1920;
    auto invalid_nvenc_core = std::make_shared<mkvc::gpu::GpuFrameCore>(
        invalid_nvenc_desc, nvenc_ready, [](uint64_t) {}, native,
        mkvc::gpu::BackendResource{mkvc::gpu::BackendResourceKind::kNvidiaCudaFrame,
                                   reinterpret_cast<void*>(native.handles[0])});
    assert(mkvc::gpu::nvidia::prepare_nvenc_cuda_frame(invalid_nvenc_core, 1920, 1080, nvenc_view,
                                                       error) == MKVC_ERROR_INVALID_ARGUMENT);
    auto no_completion_core = std::make_shared<mkvc::gpu::GpuFrameCore>(
        desc, nullptr, [](uint64_t) {}, native,
        mkvc::gpu::BackendResource{mkvc::gpu::BackendResourceKind::kNvidiaCudaFrame,
                                   reinterpret_cast<void*>(native.handles[0])});
    assert(mkvc::gpu::nvidia::prepare_nvenc_cuda_frame(no_completion_core, 1920, 1080, nvenc_view,
                                                       error) == MKVC_ERROR_INVALID_STATE);

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

    auto dl_ready = std::make_shared<mkvc::gpu::ManualCompletion>();
    dl_ready->complete();
    unsigned dl_recycled = 0;
    auto dl_core = std::make_shared<mkvc::gpu::GpuFrameCore>(
        desc, dl_ready, [&](uint64_t) { ++dl_recycled; }, native);
    mkvc_gpu_frame* dl_handle = mkvc::gpu::make_handle(dl_core);
    void* opaque_tensor = nullptr;
    assert(mkvc_gpu_frame_export_dlpack(dl_handle, 1, 0, &opaque_tensor) == MKVC_OK);
    auto* tensor = static_cast<TestDLManagedTensor*>(opaque_tensor);
    assert(tensor->dl_tensor.data ==
           reinterpret_cast<void*>(native.handles[0] + desc.plane_offsets[1]));
    assert(tensor->dl_tensor.device.type == 2 && tensor->dl_tensor.device.id == 7);
    assert(tensor->dl_tensor.ndim == 2 && tensor->dl_tensor.dtype.code == 1 &&
           tensor->dl_tensor.dtype.bits == 8 && tensor->dl_tensor.dtype.lanes == 1);
    assert(tensor->dl_tensor.shape[0] == 540 && tensor->dl_tensor.shape[1] == 1920);
    assert(tensor->dl_tensor.strides[0] == 2048 && tensor->dl_tensor.strides[1] == 1);
    mkvc_gpu_frame_release(dl_handle);
    assert(dl_core->external_leases() == 1 && dl_recycled == 0);
    tensor->deleter(tensor);
    assert(dl_core->external_leases() == 0 && dl_recycled == 1);

    // Linear Intel device-USM is a real oneAPI DLPack pointer. Native
    // D3D11/VA resources must never enter this branch.
    auto usm_desc = desc;
    usm_desc.backend = MKVC_BACKEND_INTEL;
    usm_desc.memory_type = MKVC_GPU_MEMORY_USM;
    usm_desc.device_id = 3;
    usm_desc.generation = 43;
    auto usm_native = native;
    usm_native.type = MKVC_GPU_NATIVE_USM_POINTER;
    usm_native.device_id = usm_desc.device_id;
    usm_native.generation = usm_desc.generation;
    usm_native.handles[0] = 0x22340000;
    usm_native.handles[1] = 0x66780000;
    usm_native.handles[2] = 0x77890000;
    usm_native.handles[3] = 0x889A0000;
    auto usm_ready = std::make_shared<mkvc::gpu::ManualCompletion>();
    auto usm_core =
        std::make_shared<mkvc::gpu::GpuFrameCore>(usm_desc, usm_ready, [](uint64_t) {}, usm_native);
    mkvc_gpu_frame* usm_handle = mkvc::gpu::make_handle(usm_core);
    opaque_tensor = nullptr;
    DependencyProbe dependency_probe;
    assert(mkvc_gpu_frame_export_dlpack_with_dependency(usm_handle, 0, 0x99AB0000, nullptr, nullptr,
                                                        &opaque_tensor) ==
           MKVC_ERROR_INVALID_ARGUMENT);
    assert(mkvc_gpu_frame_export_dlpack_with_dependency(
               usm_handle, 0, 0x99AB0000, reject_dependency, nullptr, &opaque_tensor) ==
           MKVC_ERROR_NOT_SUPPORTED);
    assert(opaque_tensor == nullptr);
    assert(mkvc_gpu_frame_export_dlpack_with_dependency(usm_handle, 0, 0x99AB0000,
                                                        register_dependency, &dependency_probe,
                                                        &opaque_tensor) == MKVC_OK);
    assert(dependency_probe.calls == 1 && dependency_probe.event == usm_native.handles[3] &&
           dependency_probe.stream == 0x99AB0000);
    tensor = static_cast<TestDLManagedTensor*>(opaque_tensor);
    assert(tensor->dl_tensor.data == reinterpret_cast<void*>(usm_native.handles[0]));
    assert(tensor->dl_tensor.device.type == 14 && tensor->dl_tensor.device.id == 3);
    assert(tensor->dl_tensor.shape[0] == 1080 && tensor->dl_tensor.shape[1] == 1920);
    // Export returned while the producer was pending. Complete it before the
    // last lease is destroyed so the frame core can shut down normally.
    usm_ready->complete();
    mkvc_gpu_frame_release(usm_handle);
    tensor->deleter(tensor);

    mkvc_gpu_native_handle_desc platform{};
    assert(mkvc::gpu::intel::make_d3d11_handle(1, 2, 0x1000, 3, platform, error) == MKVC_OK);
    assert(platform.type == MKVC_GPU_NATIVE_D3D11_TEXTURE && platform.handles[1] == 3);
    assert(mkvc::gpu::intel::make_va_surface_handle(1, 2, 0x2000, 17, platform, error) == MKVC_OK);
    assert(platform.type == MKVC_GPU_NATIVE_VA_SURFACE && platform.handles[1] == 17);
    assert(mkvc::gpu::nvidia::make_cuda_handle(1, 2, MKVC_GPU_NATIVE_CUDA_ARRAY, 0x3000, 0x4000,
                                               0x5000, 0x6000, platform, error) == MKVC_OK);
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
    mkvc::gpu::CallbackCompletion callback([&polls](bool& complete, std::string&) {
        complete = ++polls >= 3;
        return MKVC_OK;
    });
    assert(callback.query(error) == MKVC_GPU_COMPLETION_PENDING);
    assert(callback.wait(50, error) == MKVC_OK);
    assert(polls >= 3);
    mkvc::gpu::CallbackCompletion never([](bool& complete, std::string&) {
        complete = false;
        return MKVC_OK;
    });
    assert(never.wait(0, error) == MKVC_ERROR_TIMEOUT);

    ExternalState external_state;
    mkvc_gpu_external_frame_config external_config{};
    external_config.struct_size = sizeof(external_config);
    external_config.struct_version = 1;
    external_config.frame = desc;
    external_config.native_handle = native;
    external_config.query = query_external;
    external_config.release = release_external;
    external_config.user_data = &external_state;
    mkvc_gpu_frame* external_frame = nullptr;
    assert(mkvc_gpu_frame_import_external(&external_config, &external_frame) == MKVC_OK);
    assert(external_frame != nullptr);
    status = 99;
    assert(mkvc_gpu_frame_query_completion(external_frame, &status) == MKVC_OK);
    assert(status == MKVC_GPU_COMPLETION_PENDING);
    mkvc_gpu_frame_desc external_desc{};
    external_desc.struct_size = sizeof(external_desc);
    external_desc.struct_version = 1;
    assert(mkvc_gpu_frame_get_desc(external_frame, &external_desc) == MKVC_OK);
    assert(external_desc.generation == desc.generation);
    external_state.complete.store(true);
    assert(mkvc_gpu_frame_wait(external_frame, 50) == MKVC_OK);
    mkvc_gpu_frame_release(external_frame);
    assert(external_state.releases.load() == 1);
    external_config.release = nullptr;
    assert(mkvc_gpu_frame_import_external(&external_config, &external_frame) ==
           MKVC_ERROR_INVALID_ARGUMENT);
    assert(external_frame == nullptr);
    external_config.release = release_external;
    external_config.query = nullptr;
    external_config.native_handle.handles[3] = 0;
    assert(mkvc_gpu_frame_import_cuda_event(&external_config, &external_frame) ==
           MKVC_ERROR_INVALID_ARGUMENT);
    assert(external_frame == nullptr && external_state.releases.load() == 1);
    external_config.frame.memory_type = MKVC_GPU_MEMORY_CUDA_ARRAY;
    external_config.frame.pitches[0] = external_config.frame.pitches[1] =
        external_config.frame.width;
    external_config.frame.plane_offsets[1] =
        static_cast<uint64_t>(external_config.frame.width) * external_config.frame.height;
    external_config.native_handle.type = MKVC_GPU_NATIVE_CUDA_ARRAY;
    external_config.native_handle.handles[0] = 0x87650000;
    assert(mkvc_gpu_frame_import_external(&external_config, &external_frame) == MKVC_OK);
    assert(external_frame != nullptr);
    mkvc_gpu_frame_release(external_frame);
    assert(external_state.releases.load() == 2);
    external_config.frame.backend = MKVC_BACKEND_INTEL;
    external_config.frame.memory_type = MKVC_GPU_MEMORY_USM;
    external_config.frame.device_id = 3;
    external_config.frame.generation = 99;
    external_config.frame.pitches[0] = external_config.frame.pitches[1] = 2048;
    external_config.frame.plane_offsets[0] = 0;
    external_config.frame.plane_offsets[1] = 2048 * 1080;
    external_config.native_handle.type = MKVC_GPU_NATIVE_USM_POINTER;
    external_config.native_handle.device_id = 3;
    external_config.native_handle.generation = 99;
    external_config.native_handle.handles[0] = 0x12340000;
    external_config.native_handle.handles[1] = 0x22340000;
    external_config.native_handle.handles[2] = 0x32340000;
    external_config.native_handle.handles[3] = 0x42340000;
    assert(mkvc_gpu_frame_import_external(&external_config, &external_frame) ==
           MKVC_ERROR_INVALID_ARGUMENT);
    assert(external_frame == nullptr && external_state.releases.load() == 2);
    external_config.frame.backend = MKVC_BACKEND_INTEL;
    external_config.frame.memory_type = MKVC_GPU_MEMORY_VA_SURFACE;
    external_config.native_handle.type = MKVC_GPU_NATIVE_VA_SURFACE;
    external_config.native_handle.handles[0] = 0x55550000;
    external_config.native_handle.handles[1] = 17;
    // This isolated unit target intentionally has no Intel runtime enabled.
    assert(mkvc_gpu_frame_import_va_surface(&external_config, &external_frame) ==
           MKVC_ERROR_NOT_SUPPORTED);
    assert(external_frame == nullptr && external_state.releases.load() == 2);
    external_config.query = query_external;
    assert(mkvc_gpu_frame_import_va_surface(&external_config, &external_frame) ==
           MKVC_ERROR_INVALID_ARGUMENT);
    external_config.query = nullptr;
    external_config.native_handle.handles[1] = UINT32_MAX;
    assert(mkvc_gpu_frame_import_va_surface(&external_config, &external_frame) ==
           MKVC_ERROR_INVALID_ARGUMENT);
    external_config.native_handle.handles[1] = 17;
    assert(mkvc_gpu_frame_import_external(&external_config, &external_frame) == MKVC_OK);
    assert(external_frame != nullptr);
    mkvc_gpu_frame_release(external_frame);
    assert(external_state.releases.load() == 3);

    auto pool = std::make_shared<mkvc::gpu::GpuFramePool>(2);
    auto pool_done_a = std::make_shared<mkvc::gpu::ManualCompletion>();
    auto pool_done_b = std::make_shared<mkvc::gpu::ManualCompletion>();
    mkvc::gpu::GpuFramePool::Acquisition acquired_a;
    mkvc::gpu::GpuFramePool::Acquisition acquired_b;
    mkvc::gpu::GpuFramePool::Acquisition blocked;
    unsigned resources_released = 0;
    assert(pool->acquire(
               desc, pool_done_a, native, [&] { ++resources_released; }, acquired_a, error) ==
           MKVC_OK);
    assert(pool->acquire(
               desc, pool_done_b, native, [&] { ++resources_released; }, acquired_b, error) ==
           MKVC_OK);
    assert(pool->in_use() == 2 && pool->peak_in_use() == 2);
    assert(pool->acquire(desc, std::make_shared<mkvc::gpu::ManualCompletion>(), native, {}, blocked,
                         error) == MKVC_WOULD_BLOCK);
    mkvc_gpu_frame* pooled_handle = mkvc::gpu::make_handle(acquired_a.core);
    pool_done_a->complete();
    acquired_a.core->poll_recycle();
    assert(pool->in_use() == 2);  // external lease still owns slot
    mkvc_gpu_frame_release(pooled_handle);
    assert(pool->in_use() == 1);
    const uint64_t first_generation = acquired_a.generation;
    acquired_a.core.reset();
    auto pool_done_c = std::make_shared<mkvc::gpu::ManualCompletion>();
    assert(pool->acquire(
               desc, pool_done_c, native, [&] { ++resources_released; }, blocked, error) ==
           MKVC_OK);
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
    assert(pool->acquire(
               desc, abandoned_done, native, [&] { ++abandoned_releases; }, abandoned, error) ==
           MKVC_OK);
    std::thread complete_abandoned([abandoned_done] { abandoned_done->complete(); });
    abandoned.core.reset();  // destructor waits and releases backend resource
    complete_abandoned.join();
    assert(abandoned_releases == 1 && pool->in_use() == 0);
    return 0;
}
