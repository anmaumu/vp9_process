#pragma once

#include "../gpu_frame.hpp"

#include <memory>
#include <string>

namespace mkvc::gpu::intel {

using ZeEventQueryStatus = uint32_t (*)(void* event);

/** Build a borrowed Level Zero event completion, primarily for unit tests. */
std::shared_ptr<Completion> make_level_zero_event_completion(
    void* event, ZeEventQueryStatus query,
    std::shared_ptr<void> library_keepalive = {});

/** Load the Level Zero loader and bind a borrowed ze_event_handle_t. */
mkvc_result load_level_zero_event_completion(
    uint64_t event, std::shared_ptr<Completion>& completion,
    std::string& error);

}  // namespace mkvc::gpu::intel
