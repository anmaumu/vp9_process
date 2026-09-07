/**
 * @file decoder_c_api_support.hpp
 * @brief Decoder-specific C ABI validation, policy, and metric helpers.
 */
#pragma once

#include <memory>
#include <string>

#include "mkvcodec/mkvc.h"

struct mkvc_decoder;

namespace mkvc::decoder::capi {

/** Return whether a versioned decoder configuration is safe to read and use. */
bool valid_decoder_config(const mkvc_decoder_config* config) noexcept;

/** Create a decoder handle and start its configured prefetch worker. */
mkvc_result create_decoder(const mkvc_decoder_config& config,
                           std::unique_ptr<mkvc_decoder>& decoder, std::string& error);

/** Apply copy constraints before the decoder accepts its first frame. */
mkvc_result set_copy_policy(mkvc_decoder& decoder, const mkvc_copy_policy& policy,
                            std::string& error);

/** Check whether the current copy policy permits a CPU frame read. */
mkvc_result check_cpu_read(const mkvc_decoder& decoder, std::string& error);

/** Check whether the selected backend can return a direct GPU frame. */
mkvc_result check_gpu_read(const mkvc_decoder& decoder, std::string& error);

/** Copy a consistent decoder metrics snapshot to a validated destination. */
void snapshot_metrics(const mkvc_decoder& decoder, mkvc_pipeline_metrics& metrics);

}  // namespace mkvc::decoder::capi
