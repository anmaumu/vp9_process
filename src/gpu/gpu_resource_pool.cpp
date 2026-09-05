#include "gpu_resource_pool.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <new>

extern thread_local std::string mkvc_last_error;

namespace mkvc::gpu {

GpuResourcePool::GpuResourcePool(uint32_t capacity) : slots_(capacity) {}

mkvc_result GpuResourcePool::acquire(
    uint32_t timeout_ms, std::shared_ptr<GpuResourceReservation>& output,
    std::string& error) {
    output.reset();
    std::unique_lock<std::mutex> lock(mutex_);
    const auto available = [this] {
        return std::any_of(slots_.begin(), slots_.end(),
                           [](const Slot& slot) { return !slot.in_use; });
    };
    if (!available()) {
        const auto wait_started = std::chrono::steady_clock::now();
        if (timeout_ms == 0) {
            ++rejected_acquisitions_;
            error = "external GPU resource pool is full";
            return MKVC_WOULD_BLOCK;
        }
        if (timeout_ms == std::numeric_limits<uint32_t>::max()) {
            changed_.wait(lock, available);
        } else if (!changed_.wait_for(
                       lock, std::chrono::milliseconds(timeout_ms), available)) {
            wait_ns_ += static_cast<uint64_t>(std::chrono::duration_cast<
                std::chrono::nanoseconds>(std::chrono::steady_clock::now() -
                                         wait_started).count());
            ++rejected_acquisitions_;
            error = "external GPU resource pool acquire timed out";
            return MKVC_ERROR_TIMEOUT;
        }
        wait_ns_ += static_cast<uint64_t>(std::chrono::duration_cast<
            std::chrono::nanoseconds>(std::chrono::steady_clock::now() -
                                     wait_started).count());
    }
    const auto iterator = std::find_if(
        slots_.begin(), slots_.end(), [](const Slot& slot) { return !slot.in_use; });
    const uint32_t index = static_cast<uint32_t>(iterator - slots_.begin());
    iterator->in_use = true;
    if (++iterator->generation == 0) ++iterator->generation;
    const uint64_t generation = iterator->generation;
    ++in_use_;
    ++acquisitions_;
    peak_in_use_ = std::max(peak_in_use_, in_use_);
    lock.unlock();
    try {
        output = std::make_shared<GpuResourceReservation>(
            shared_from_this(), index, generation);
        return MKVC_OK;
    } catch (...) {
        release(index, generation);
        throw;
    }
}

GpuResourcePool::Snapshot GpuResourcePool::snapshot() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return {static_cast<uint32_t>(slots_.size()), in_use_, peak_in_use_,
            acquisitions_, rejected_acquisitions_, wait_ns_};
}

void GpuResourcePool::release(uint32_t slot, uint64_t generation) noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (slot >= slots_.size()) return;
        auto& candidate = slots_[slot];
        if (!candidate.in_use || candidate.generation != generation) return;
        candidate.in_use = false;
        if (in_use_ != 0) --in_use_;
    }
    changed_.notify_one();
}

GpuResourceReservation::GpuResourceReservation(
    std::shared_ptr<GpuResourcePool> pool, uint32_t slot,
    uint64_t generation) noexcept
    : pool_(std::move(pool)), slot_(slot), generation_(generation) {}

GpuResourceReservation::~GpuResourceReservation() {
    if (pool_) pool_->release(slot_, generation_);
}

}  // namespace mkvc::gpu

struct mkvc_gpu_resource_pool {
    std::shared_ptr<mkvc::gpu::GpuResourcePool> implementation;
};
struct mkvc_gpu_resource_reservation {
    std::shared_ptr<mkvc::gpu::GpuResourceReservation> implementation;
};

namespace {
mkvc_result pool_fail(mkvc_result result, const char* message) {
    mkvc_last_error = message;
    return result;
}
}

extern "C" mkvc_result mkvc_gpu_resource_pool_create(
    const mkvc_gpu_resource_pool_config* config,
    mkvc_gpu_resource_pool** out_pool) {
    if (out_pool == nullptr) return pool_fail(MKVC_ERROR_INVALID_ARGUMENT, "null GPU pool output");
    *out_pool = nullptr;
    if (config == nullptr || config->struct_size < sizeof(*config) ||
        config->struct_version != 1 || config->capacity == 0 ||
        config->reserved != 0) {
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
    try { delete pool; } catch (...) {}
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
        const mkvc_result result = pool->implementation->acquire(
            timeout_ms, reservation, error);
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

extern "C" void mkvc_gpu_resource_reservation_release(
    mkvc_gpu_resource_reservation* reservation) {
    try { delete reservation; } catch (...) {}
}

extern "C" mkvc_result mkvc_gpu_resource_pool_get_stats(
    const mkvc_gpu_resource_pool* pool, mkvc_gpu_resource_pool_stats* out_stats) {
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
