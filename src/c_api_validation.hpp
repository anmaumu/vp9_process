/**
 * @file c_api_validation.hpp
 * @brief Shared validation for versioned C ABI structures.
 */
#pragma once

#include "mkvcodec/mkvc.h"

namespace mkvc::capi {

/** Return whether a versioned copy policy is safe to read. */
inline bool valid_copy_policy(const mkvc_copy_policy* policy) noexcept {
    return policy != nullptr && policy->struct_size >= sizeof(mkvc_copy_policy) &&
           policy->struct_version == 1;
}

/** Return whether a versioned metrics destination is safe to overwrite. */
inline bool valid_metrics_output(const mkvc_pipeline_metrics* metrics) noexcept {
    return metrics != nullptr && metrics->struct_size >= sizeof(mkvc_pipeline_metrics) &&
           metrics->struct_version == 1;
}

/** Return whether a detailed timing destination is safe to overwrite. */
inline bool valid_stage_metrics_output(const mkvc_pipeline_stage_metrics* metrics) noexcept {
    return metrics != nullptr && metrics->struct_size >= sizeof(mkvc_pipeline_stage_metrics) &&
           metrics->struct_version == 1;
}

/** Return whether a component-timing destination is safe to overwrite. */
inline bool valid_component_metrics_output(
    const mkvc_pipeline_component_metrics* metrics) noexcept {
    return metrics != nullptr && metrics->struct_size >= sizeof(mkvc_pipeline_component_metrics) &&
           metrics->struct_version == 1;
}

}  // namespace mkvc::capi
