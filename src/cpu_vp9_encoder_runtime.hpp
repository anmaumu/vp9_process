#pragma once

#include <cstdint>
#include <string>

#include "cpu_vp9_encoder.hpp"

namespace mkvc::cpu_vp9_detail {

/** Initialize libvpx using the public encoder configuration. */
mkvc_result initialize_codec(CpuVp9Encoder::Impl& impl, const mkvc_encoder_config& config,
                             std::string& error);

/** Wrap and submit the converted I420 image, then collect available packets. */
mkvc_result encode_frame(CpuVp9Encoder::Impl& impl, int64_t requested_pts, std::string& error);

/** Flush delayed libvpx packets into the container. */
mkvc_result flush_codec(CpuVp9Encoder::Impl& impl, std::string& error);

/** Destroy the initialized libvpx context idempotently. */
void destroy_codec(CpuVp9Encoder::Impl& impl);

}  // namespace mkvc::cpu_vp9_detail
