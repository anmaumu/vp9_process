#pragma once

#include "mkvcodec/mkvc.h"

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace mkvc::gpu {

/** One generation-checked reservation in a bounded external GPU resource pool. */
class GpuResourceReservation;

/** Reservation-only pool; allocation remains owned by the language/vendor adapter. */
class GpuResourcePool : public std::enable_shared_from_this<GpuResourcePool> {
 public:
    struct Snapshot {
        uint32_t capacity = 0;
        uint32_t in_use = 0;
        uint32_t peak_in_use = 0;
        uint64_t acquisitions = 0;
        uint64_t rejected_acquisitions = 0;
        uint64_t wait_ns = 0;
    };

    explicit GpuResourcePool(uint32_t capacity);
    mkvc_result acquire(uint32_t timeout_ms,
                        std::shared_ptr<GpuResourceReservation>& output,
                        std::string& error);
    Snapshot snapshot() const noexcept;

 private:
    friend class GpuResourceReservation;
    struct Slot { uint64_t generation = 0; bool in_use = false; };
    void release(uint32_t slot, uint64_t generation) noexcept;

    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::vector<Slot> slots_;
    uint32_t in_use_ = 0;
    uint32_t peak_in_use_ = 0;
    uint64_t acquisitions_ = 0;
    uint64_t rejected_acquisitions_ = 0;
    uint64_t wait_ns_ = 0;
};

class GpuResourceReservation {
 public:
    GpuResourceReservation(std::shared_ptr<GpuResourcePool> pool,
                           uint32_t slot, uint64_t generation) noexcept;
    ~GpuResourceReservation();
    uint32_t slot() const noexcept { return slot_; }
    uint64_t generation() const noexcept { return generation_; }

 private:
    std::shared_ptr<GpuResourcePool> pool_;
    uint32_t slot_;
    uint64_t generation_;
};

}  // namespace mkvc::gpu
