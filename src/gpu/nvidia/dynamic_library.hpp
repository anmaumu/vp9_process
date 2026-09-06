#pragma once

#include <cstdint>
#include <cstring>

namespace mkvc::gpu::nvidia {

/**
 * @brief Non-copyable runtime library used for optional NVIDIA driver APIs.
 *
 * Driver libraries remain external deployment dependencies. The wrapper owns
 * only the operating-system module handle and returns nullptr for absent symbols.
 */
class DynamicLibrary {
   public:
    /** Open a driver library by platform-native filename. */
    explicit DynamicLibrary(const char* name);
    /** Close the module handle when one was opened. */
    ~DynamicLibrary();
    DynamicLibrary(const DynamicLibrary&) = delete;
    DynamicLibrary& operator=(const DynamicLibrary&) = delete;

    /** Return whether the requested module was opened successfully. */
    explicit operator bool() const noexcept;

    /** Resolve a function or data symbol without inventing a fallback. */
    template <typename T>
    T symbol(const char* name) const noexcept {
        const uintptr_t address = symbol_address(name);
        static_assert(sizeof(T) == sizeof(address));
        T result = nullptr;
        std::memcpy(&result, &address, sizeof(result));
        return result;
    }

   private:
    uintptr_t symbol_address(const char* name) const noexcept;
    void* handle_ = nullptr;
};

}  // namespace mkvc::gpu::nvidia
