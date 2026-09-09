/**
 * @file gpu_external_import.hpp
 * @brief Shared validation and ownership support for external GPU imports.
 */
#pragma once

#include "gpu_frame.hpp"

#include <memory>
#include <string>

namespace mkvc::gpu::external {

/** Store one stable C ABI diagnostic and return its result code. */
mkvc_result fail(mkvc_result result, std::string message);

/** Validate backend identity, native resource layout, and callback ownership. */
bool valid_layout(const mkvc_gpu_external_frame_config& config, std::string& error,
                  bool allow_usm_level_zero_event = false);

/** Create an opaque frame lease around an already-selected producer completion. */
mkvc_result import_with_completion(const mkvc_gpu_external_frame_config& config,
                                   std::shared_ptr<Completion> producer,
                                   mkvc_gpu_frame** out_frame);

}  // namespace mkvc::gpu::external
