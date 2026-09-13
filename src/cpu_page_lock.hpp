#pragma once

/**
 * @file cpu_page_lock.hpp
 * @brief Small cross-platform wrapper around process page-lock primitives.
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mkvc {

/** Move-only pageable or dedicated OS page-locked byte allocation. */
class CpuAllocation {
   public:
    CpuAllocation() = default;
    ~CpuAllocation();
    CpuAllocation(const CpuAllocation&) = delete;
    CpuAllocation& operator=(const CpuAllocation&) = delete;
    CpuAllocation(CpuAllocation&& other) noexcept;
    CpuAllocation& operator=(CpuAllocation&& other) noexcept;

    bool allocate(size_t bytes, bool page_locked, std::string& error);
    uint8_t* data() noexcept;
    const uint8_t* data() const noexcept;
    size_t size() const noexcept { return size_; }
    size_t page_locked_bytes() const noexcept { return locked_bytes_; }

   private:
    void reset() noexcept;
    std::vector<uint8_t> pageable_;
    void* locked_ = nullptr;
    size_t size_ = 0;
    size_t locked_bytes_ = 0;
};

}  // namespace mkvc
