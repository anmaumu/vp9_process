#pragma once

#include "../gpu_frame.hpp"
#include <functional>

namespace mkvc::gpu::intel {
/** Poll a monotonic fence value; UINT64_MAX represents device removal.
 * Terminal results are latched. query owns any resources required by polling.
 */
std::shared_ptr<Completion> make_d3d11_fence_completion(
    uint64_t target, std::function<uint64_t()> query);

/** Validate a same-device NV12 texture/fence and retain their COM references. */
mkvc_result load_d3d11_fence_completion(
    const mkvc_gpu_external_frame_config& config,
    std::shared_ptr<Completion>& completion, std::string& error);
}  // namespace mkvc::gpu::intel
