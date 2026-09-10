#include <memory>
#include <new>
#include <string>

#include "gpu_resource_pool.hpp"

extern thread_local std::string mkvc_last_error;

/** C ABI handle owning the language-neutral GPU resource pool implementation. */
struct mkvc_gpu_resource_pool {
    std::shared_ptr<mkvc::gpu::GpuResourcePool> implementation;
};

/** C ABI handle retaining one generation-checked pool reservation. */
struct mkvc_gpu_resource_reservation {
    std::shared_ptr<mkvc::gpu::GpuResourceReservation> implementation;
};

namespace {

mkvc_result pool_fail(mkvc_result result, const char* message) {
    mkvc_last_error = message;
    return result;
}

}  // namespace

extern "C" mkvc_result mkvc_gpu_resource_pool_create(const mkvc_gpu_resource_pool_config* config,
                                                     mkvc_gpu_resource_pool** out_pool) {
    if (out_pool == nullptr) return pool_fail(MKVC_ERROR_INVALID_ARGUMENT, "null GPU pool output");
    *out_pool = nullptr;
    if (config == nullptr || config->struct_size < sizeof(*config) || config->struct_version != 1 ||
        config->capacity == 0 || config->reserved != 0) {
        return pool_fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid external GPU pool configuration");
    }
    try {
        auto handle = std::make_unique<mkvc_gpu_resource_pool>();
        handle->implementation = std::make_shared<mkvc::gpu::GpuResourcePool>(config->capacity);
        *out_pool = handle.release();
        return MKVC_OK;
    } catch (const std::bad_alloc&) {
        return pool_fail(MKVC_ERROR_INTERNAL, "external GPU pool allocation failed");
    } catch (...) {
        return pool_fail(MKVC_ERROR_INTERNAL, "unknown external GPU pool failure");
    }
}

extern "C" void mkvc_gpu_resource_pool_destroy(mkvc_gpu_resource_pool* pool) {
    try {
        delete pool;
    } catch (...) {
    }
}

extern "C" mkvc_result mkvc_gpu_resource_pool_acquire(
    mkvc_gpu_resource_pool* pool, uint32_t timeout_ms,
    mkvc_gpu_resource_reservation** out_reservation) {
    if (out_reservation == nullptr)
        return pool_fail(MKVC_ERROR_INVALID_ARGUMENT, "null GPU reservation output");
    *out_reservation = nullptr;
    if (pool == nullptr || !pool->implementation)
        return pool_fail(MKVC_ERROR_INVALID_STATE, "external GPU pool is closed");
    try {
        std::shared_ptr<mkvc::gpu::GpuResourceReservation> reservation;
        std::string error;
        const mkvc_result result = pool->implementation->acquire(timeout_ms, reservation, error);
        if (result != MKVC_OK) {
            mkvc_last_error = error;
            return result;
        }
        auto handle = std::make_unique<mkvc_gpu_resource_reservation>();
        handle->implementation = std::move(reservation);
        *out_reservation = handle.release();
        return MKVC_OK;
    } catch (const std::bad_alloc&) {
        return pool_fail(MKVC_ERROR_INTERNAL, "GPU reservation allocation failed");
    } catch (...) {
        return pool_fail(MKVC_ERROR_INTERNAL, "unknown GPU reservation failure");
    }
}

extern "C" mkvc_result mkvc_gpu_resource_reservation_get_desc(
    const mkvc_gpu_resource_reservation* reservation,
    mkvc_gpu_resource_reservation_desc* out_desc) {
    if (reservation == nullptr || !reservation->implementation || out_desc == nullptr ||
        out_desc->struct_size < sizeof(*out_desc) || out_desc->struct_version != 1) {
        return pool_fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid GPU reservation descriptor query");
    }
    out_desc->slot_index = reservation->implementation->slot();
    out_desc->reserved = 0;
    out_desc->generation = reservation->implementation->generation();
    return MKVC_OK;
}

extern "C" void mkvc_gpu_resource_reservation_release(mkvc_gpu_resource_reservation* reservation) {
    try {
        delete reservation;
    } catch (...) {
    }
}

extern "C" mkvc_result mkvc_gpu_resource_pool_get_stats(const mkvc_gpu_resource_pool* pool,
                                                        mkvc_gpu_resource_pool_stats* out_stats) {
    if (pool == nullptr || !pool->implementation || out_stats == nullptr ||
        out_stats->struct_size < sizeof(*out_stats) || out_stats->struct_version != 1) {
        return pool_fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid external GPU pool stats query");
    }
    const auto value = pool->implementation->snapshot();
    out_stats->capacity = value.capacity;
    out_stats->in_use = value.in_use;
    out_stats->peak_in_use = value.peak_in_use;
    out_stats->reserved = 0;
    out_stats->acquisitions = value.acquisitions;
    out_stats->rejected_acquisitions = value.rejected_acquisitions;
    out_stats->wait_ns = value.wait_ns;
    return MKVC_OK;
}
