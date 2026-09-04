// Test-only host interop probe, built against the isolated dpctl runtime.
#include <sycl/sycl.hpp>
#include <sycl/ext/oneapi/backend/level_zero.hpp>
#include <level_zero/ze_api.h>
#include <fcntl.h>

namespace {
struct EventHolder { sycl::event event; };
}

extern "C" int mkvc_test_sycl_barrier_event(
    void* queue_ref, void** event_owner, void** native_event) noexcept {
    if (!queue_ref || !event_owner || !native_event) return -1;
    *event_owner = nullptr;
    *native_event = nullptr;
    try {
        auto& queue = *static_cast<sycl::queue*>(queue_ref);
        if (queue.get_backend() != sycl::backend::ext_oneapi_level_zero) return -2;
        auto* holder = new EventHolder{queue.ext_oneapi_submit_barrier()};
        *native_event = sycl::get_native<
            sycl::backend::ext_oneapi_level_zero>(holder->event);
        if (!*native_event) { delete holder; return -4; }
        *event_owner = holder;
        return 0;
    } catch (...) { return -3; }
}

extern "C" int mkvc_test_sycl_event_free(void* event_owner) noexcept {
    if (!event_owner) return -1;
    try { delete static_cast<EventHolder*>(event_owner); return 0; }
    catch (...) { return -3; }
}

extern "C" int mkvc_test_sycl_queue_wait_event(
    void* queue_ref, void* native_event) noexcept {
    if (!queue_ref || !native_event) return -1;
    try {
        auto& queue = *static_cast<sycl::queue*>(queue_ref);
        if (queue.get_backend() != sycl::backend::ext_oneapi_level_zero) return -2;
        sycl::backend_input_t<sycl::backend::ext_oneapi_level_zero, sycl::event>
            input{static_cast<ze_event_handle_t>(native_event),
                  sycl::ext::oneapi::level_zero::ownership::keep};
        auto dependency = sycl::make_event<
            sycl::backend::ext_oneapi_level_zero>(input, queue.get_context());
        queue.ext_oneapi_submit_barrier({dependency});
        return 0;
    } catch (...) { return -3; }
}

extern "C" int mkvc_test_sycl_alloc_exportable(void* queue_ref, uint64_t bytes, void** pointer) noexcept {
    if (!queue_ref || !bytes || !pointer) return -1;
    *pointer = nullptr;
    try {
        auto& queue = *static_cast<sycl::queue*>(queue_ref);
        if (queue.get_backend() != sycl::backend::ext_oneapi_level_zero) return -2;
        auto context = sycl::get_native<sycl::backend::ext_oneapi_level_zero>(queue.get_context());
        auto device = sycl::get_native<sycl::backend::ext_oneapi_level_zero>(queue.get_device());
        ze_external_memory_export_desc_t external{};
        external.stype = ZE_STRUCTURE_TYPE_EXTERNAL_MEMORY_EXPORT_DESC;
        external.flags = ZE_EXTERNAL_MEMORY_TYPE_FLAG_DMA_BUF;
        ze_device_mem_alloc_desc_t desc{};
        desc.stype = ZE_STRUCTURE_TYPE_DEVICE_MEM_ALLOC_DESC;
        desc.pNext = &external;
        return static_cast<int>(zeMemAllocDevice(context, &desc, bytes, 64, device, pointer));
    } catch (...) { return -3; }
}

extern "C" int mkvc_test_sycl_free(void* queue_ref, void* pointer) noexcept {
    if (!queue_ref || !pointer) return -1;
    try {
        auto& queue = *static_cast<sycl::queue*>(queue_ref);
        auto context = sycl::get_native<sycl::backend::ext_oneapi_level_zero>(queue.get_context());
        return static_cast<int>(zeMemFree(context, pointer));
    } catch (...) { return -3; }
}

extern "C" int mkvc_test_sycl_export_fd(void* queue_ref, void* pointer, int* fd,
                                       uint64_t* bytes, uint64_t* offset) noexcept {
    if (!queue_ref || !pointer || !fd || !bytes || !offset) return -1;
    *fd = -1;
    *bytes = 0;
    *offset = 0;
    try {
        auto& queue = *static_cast<sycl::queue*>(queue_ref);
        if (queue.get_backend() != sycl::backend::ext_oneapi_level_zero ||
            sycl::get_pointer_type(pointer, queue.get_context()) != sycl::usm::alloc::device)
            return -2;
        auto context = sycl::get_native<sycl::backend::ext_oneapi_level_zero>(queue.get_context());
        void* base = nullptr;
        size_t size = 0;
        auto range = zeMemGetAddressRange(context, pointer, &base, &size);
        if (range != ZE_RESULT_SUCCESS) return static_cast<int>(range);
        const auto begin = reinterpret_cast<uintptr_t>(base);
        const auto address = reinterpret_cast<uintptr_t>(pointer);
        if (!size || address < begin || address - begin >= size) return -4;
        ze_external_memory_export_fd_t external{};
        external.stype = ZE_STRUCTURE_TYPE_EXTERNAL_MEMORY_EXPORT_FD;
        external.flags = ZE_EXTERNAL_MEMORY_TYPE_FLAG_DMA_BUF;
        external.fd = -1;
        ze_memory_allocation_properties_t properties{};
        properties.stype = ZE_STRUCTURE_TYPE_MEMORY_ALLOCATION_PROPERTIES;
        properties.pNext = &external;
        const auto result = zeMemGetAllocProperties(context, base, &properties, nullptr);
        if (result == ZE_RESULT_SUCCESS) {
            // Level Zero owns the exported fd; only our duplicate may be closed.
            *fd = fcntl(external.fd, F_DUPFD_CLOEXEC, 0);
            if (*fd < 0) return -5;
            *bytes = size;
            *offset = address - begin;
        }
        return static_cast<int>(result);
    } catch (...) { return -3; }
}
