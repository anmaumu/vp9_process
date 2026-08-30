#include "mkvcodec/mkvc.h"

#include <ffnvcodec/dynlink_cuda.h>

#include <atomic>
#include <cstdint>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {
std::atomic<unsigned> releases{0};
void release_frame(void*) { releases.fetch_add(1); }

#ifdef _WIN32
using Library = HMODULE;
void* symbol(Library library, const char* name) {
    return reinterpret_cast<void*>(GetProcAddress(library, name));
}
#else
using Library = void*;
void* symbol(Library library, const char* name) { return dlsym(library, name); }
#endif
}

int main() {
#ifdef _WIN32
    Library library = LoadLibraryA("nvcuda.dll");
#else
    Library library = dlopen("libcuda.so.1", RTLD_NOW | RTLD_LOCAL);
#endif
    if (library == nullptr) return 77;
    auto init = reinterpret_cast<tcuInit*>(symbol(library, "cuInit"));
    auto device_get = reinterpret_cast<tcuDeviceGet*>(symbol(library, "cuDeviceGet"));
    auto context_create = reinterpret_cast<tcuCtxCreate_v2*>(symbol(library, "cuCtxCreate_v2"));
    auto context_pop = reinterpret_cast<tcuCtxPopCurrent_v2*>(symbol(library, "cuCtxPopCurrent_v2"));
    auto context_push = reinterpret_cast<tcuCtxPushCurrent_v2*>(symbol(library, "cuCtxPushCurrent_v2"));
    auto context_destroy = reinterpret_cast<tcuCtxDestroy_v2*>(symbol(library, "cuCtxDestroy_v2"));
    auto event_create = reinterpret_cast<tcuEventCreate*>(symbol(library, "cuEventCreate"));
    auto event_record = reinterpret_cast<tcuEventRecord*>(symbol(library, "cuEventRecord"));
    auto event_destroy = reinterpret_cast<tcuEventDestroy_v2*>(symbol(library, "cuEventDestroy_v2"));
    auto stream_create = reinterpret_cast<tcuStreamCreate*>(symbol(library, "cuStreamCreate"));
    auto stream_sync = reinterpret_cast<tcuStreamSynchronize*>(symbol(library, "cuStreamSynchronize"));
    auto stream_destroy = reinterpret_cast<tcuStreamDestroy_v2*>(symbol(library, "cuStreamDestroy_v2"));
    if (!init || !device_get || !context_create || !context_pop || !context_push ||
        !context_destroy || !event_create || !event_record ||
        !event_destroy || !stream_create || !stream_sync || !stream_destroy ||
        init(0) != CUDA_SUCCESS) return 77;

    CUdevice device = 0;
    CUcontext context = nullptr;
    CUevent event = nullptr;
    CUstream stream = nullptr;
    if (device_get(&device, 0) != CUDA_SUCCESS ||
        context_create(&context, 0, device) != CUDA_SUCCESS ||
        event_create(&event, CU_EVENT_DISABLE_TIMING) != CUDA_SUCCESS ||
        stream_create(&stream, CU_STREAM_NON_BLOCKING) != CUDA_SUCCESS ||
        event_record(event, nullptr) != CUDA_SUCCESS) return 77;
    CUcontext popped = nullptr;
    if (context_pop(&popped) != CUDA_SUCCESS || popped != context) return 1;

    mkvc_gpu_external_frame_config config{};
    config.struct_size = sizeof(config);
    config.struct_version = 1;
    config.frame.struct_size = sizeof(config.frame);
    config.frame.struct_version = 1;
    config.frame.backend = MKVC_BACKEND_NVIDIA;
    config.frame.memory_type = MKVC_GPU_MEMORY_CUDA_POINTER;
    config.frame.generation = 1;
    config.frame.pixel_format = MKVC_PIXEL_FORMAT_NV12;
    config.frame.width = 64;
    config.frame.height = 48;
    config.frame.plane_count = 2;
    config.frame.pitches[0] = config.frame.pitches[1] = 64;
    config.frame.plane_offsets[1] = 64 * 48;
    config.native_handle.struct_size = sizeof(config.native_handle);
    config.native_handle.struct_version = 1;
    config.native_handle.type = MKVC_GPU_NATIVE_CUDA_POINTER;
    config.native_handle.borrowed = 1;
    config.native_handle.generation = 1;
    config.native_handle.handles[0] = 1;  // Completion test; never submitted.
    config.native_handle.handles[1] = reinterpret_cast<uintptr_t>(context);
    config.native_handle.handles[3] = reinterpret_cast<uintptr_t>(event);
    config.release = release_frame;

    mkvc_gpu_frame* frame = nullptr;
    if (mkvc_gpu_frame_import_cuda_event(&config, &frame) != MKVC_OK ||
        frame == nullptr) {
        std::cerr << mkvc_get_last_error() << '\n';
        return 1;
    }
    void* managed_tensor = nullptr;
    if (mkvc_gpu_frame_export_dlpack(
            frame, 0, reinterpret_cast<uintptr_t>(stream),
            &managed_tensor) != MKVC_OK || managed_tensor == nullptr) {
        std::cerr << mkvc_get_last_error() << '\n';
        return 1;
    }
    mkvc_dlpack_managed_tensor_release(managed_tensor);
    if (context_push(context) != CUDA_SUCCESS ||
        stream_sync(stream) != CUDA_SUCCESS ||
        context_pop(&popped) != CUDA_SUCCESS || popped != context ||
        mkvc_gpu_frame_wait(frame, 1000) != MKVC_OK) return 1;
    mkvc_gpu_frame_release(frame);
    if (releases.load() != 1) return 1;

    if (context_push(context) != CUDA_SUCCESS ||
        stream_sync(stream) != CUDA_SUCCESS ||
        stream_destroy(stream) != CUDA_SUCCESS ||
        event_destroy(event) != CUDA_SUCCESS ||
        context_pop(&popped) != CUDA_SUCCESS ||
        context_destroy(context) != CUDA_SUCCESS) return 1;
#ifdef _WIN32
    FreeLibrary(library);
#else
    dlclose(library);
#endif
    std::cout << "CUDA event import completed without device synchronization\n";
    return 0;
}
