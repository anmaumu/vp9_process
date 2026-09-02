#pragma once

#include "../gpu_frame.hpp"

namespace mkvc::gpu::intel {

// Minimal libva C ABI, verified against va/va.h. No vendor objects or headers
// cross the public ABI, and libva remains a runtime dependency, not a payload.
// Reference: https://github.com/intel/libva/blob/2.23.0/va/va.h
using VaSyncSurface2 = int (*)(void*, unsigned int, uint64_t);
constexpr int kVaSuccess = 0;
constexpr int kVaUnimplemented = 0x14;
constexpr int kVaTimedOut = 0x26;

/** Poll one VA surface with timeout_ns=0; terminal results are latched.
 * Only VA-submitted work is covered, not arbitrary OpenCL/SYCL writes.
 */
std::shared_ptr<Completion> make_va_surface_completion(
    void* display, uint32_t surface, VaSyncSurface2 sync,
    std::shared_ptr<void> library_keepalive = {});

/** Load libva.so.2 and probe vaSyncSurface2 without a blocking fallback. */
mkvc_result load_va_surface_completion(
    uint64_t display, uint64_t surface,
    std::shared_ptr<Completion>& completion, std::string& error);

}  // namespace mkvc::gpu::intel
