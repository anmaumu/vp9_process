#include "dynamic_library.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace mkvc::gpu::nvidia {

DynamicLibrary::DynamicLibrary(const char* name) {
#if defined(_WIN32)
    handle_ = reinterpret_cast<void*>(LoadLibraryA(name));
#else
    handle_ = dlopen(name, RTLD_LAZY | RTLD_LOCAL);
#endif
}

DynamicLibrary::~DynamicLibrary() {
#if defined(_WIN32)
    if (handle_ != nullptr) FreeLibrary(reinterpret_cast<HMODULE>(handle_));
#else
    if (handle_ != nullptr) dlclose(handle_);
#endif
}

DynamicLibrary::operator bool() const noexcept { return handle_ != nullptr; }

uintptr_t DynamicLibrary::symbol_address(const char* name) const noexcept {
    if (handle_ == nullptr || name == nullptr) return 0;
#if defined(_WIN32)
    return reinterpret_cast<uintptr_t>(GetProcAddress(reinterpret_cast<HMODULE>(handle_), name));
#else
    return reinterpret_cast<uintptr_t>(dlsym(handle_, name));
#endif
}

}  // namespace mkvc::gpu::nvidia
