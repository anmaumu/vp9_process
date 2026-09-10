#pragma once

#include <memory>
#include <string>

#include "cpu_vp9_decoder.hpp"

namespace mkvc::cpu_vp9_decoder_detail {

/** Open the VP9 track and initialize the configured libvpx decoder. */
mkvc_result initialize(CpuVp9Decoder::Impl& impl, const mkvc_decoder_config& config,
                       std::string& error);

/** Demux and decode the next owned I420 frame. */
mkvc_result read_frame(CpuVp9Decoder::Impl& impl, std::unique_ptr<DecodedFrame>& frame,
                       std::string& error);

/** Destroy codec and packet-reader resources idempotently. */
void destroy(CpuVp9Decoder::Impl& impl) noexcept;

}  // namespace mkvc::cpu_vp9_decoder_detail
