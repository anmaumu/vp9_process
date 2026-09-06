#include "gpu/nvidia/dynamic_library.hpp"

#include <cassert>
#include <cstddef>

int main() {
#if defined(_WIN32)
    mkvc::gpu::nvidia::DynamicLibrary library("kernel32.dll");
    const auto symbol = library.symbol<void (*)()>("GetCurrentProcessId");
#else
    mkvc::gpu::nvidia::DynamicLibrary library("libc.so.6");
    const auto symbol = library.symbol<void* (*)(size_t)>("malloc");
#endif
    assert(library);
    assert(symbol != nullptr);
    assert(library.symbol<void (*)()>("mkvc_deliberately_missing_symbol") == nullptr);
    return 0;
}
