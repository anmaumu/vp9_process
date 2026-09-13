/**
 * @file cpu_page_lock.cpp
 * @brief Dedicated pageable and OS page-locked CPU allocations.
 */
#include "cpu_page_lock.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <string>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace mkvc {

namespace {
size_t page_size() noexcept {
#ifdef _WIN32
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    return info.dwPageSize;
#else
    const long value = sysconf(_SC_PAGESIZE);
    return value > 0 ? static_cast<size_t>(value) : 4096;
#endif
}
}  // namespace

CpuAllocation::~CpuAllocation() { reset(); }

CpuAllocation::CpuAllocation(CpuAllocation&& other) noexcept
    : pageable_(std::move(other.pageable_)),
      locked_(other.locked_),
      size_(other.size_),
      locked_bytes_(other.locked_bytes_) {
    other.locked_ = nullptr;
    other.size_ = 0;
    other.locked_bytes_ = 0;
}

CpuAllocation& CpuAllocation::operator=(CpuAllocation&& other) noexcept {
    if (this == &other) return *this;
    reset();
    pageable_ = std::move(other.pageable_);
    locked_ = other.locked_;
    size_ = other.size_;
    locked_bytes_ = other.locked_bytes_;
    other.locked_ = nullptr;
    other.size_ = 0;
    other.locked_bytes_ = 0;
    return *this;
}

bool CpuAllocation::allocate(size_t bytes, bool page_locked, std::string& error) {
    reset();
    if (!page_locked) {
        pageable_.resize(bytes);
        size_ = bytes;
        return true;
    }
    const size_t page = page_size();
    locked_bytes_ = ((bytes + page - 1) / page) * page;
#ifdef _WIN32
    locked_ = VirtualAlloc(nullptr, locked_bytes_, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (locked_ != nullptr && VirtualLock(locked_, locked_bytes_) != 0) {
        size_ = bytes;
        return true;
    }
    const DWORD code = GetLastError();
    if (locked_ != nullptr) (void)VirtualFree(locked_, 0, MEM_RELEASE);
    locked_ = nullptr;
    error = "VirtualLock allocation failed with system error " + std::to_string(code);
#else
    locked_ =
        mmap(nullptr, locked_bytes_, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (locked_ != MAP_FAILED && mlock(locked_, locked_bytes_) == 0) {
        size_ = bytes;
        return true;
    }
    const int code = errno;
    if (locked_ != MAP_FAILED) (void)munmap(locked_, locked_bytes_);
    locked_ = nullptr;
    error = std::string("page-locked mmap failed: ") + std::strerror(code);
#endif
    locked_bytes_ = 0;
    return false;
}

uint8_t* CpuAllocation::data() noexcept {
    return locked_ != nullptr ? static_cast<uint8_t*>(locked_) : pageable_.data();
}

const uint8_t* CpuAllocation::data() const noexcept {
    return locked_ != nullptr ? static_cast<const uint8_t*>(locked_) : pageable_.data();
}

void CpuAllocation::reset() noexcept {
    pageable_.clear();
    if (locked_ == nullptr) {
        size_ = 0;
        locked_bytes_ = 0;
        return;
    }
#ifdef _WIN32
    (void)VirtualUnlock(locked_, locked_bytes_);
    (void)VirtualFree(locked_, 0, MEM_RELEASE);
#else
    (void)munlock(locked_, locked_bytes_);
    (void)munmap(locked_, locked_bytes_);
#endif
    locked_ = nullptr;
    size_ = 0;
    locked_bytes_ = 0;
}

}  // namespace mkvc
