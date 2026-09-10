#pragma once

#include <memory>
#include <string>

#include "cpu_av1_decoder.hpp"

namespace mkvc::cpu_av1_decoder_detail {

/** Open the AV1 track and initialize the configured libaom decoder. */
mkvc_result initialize(CpuAv1Decoder::Impl& impl, const mkvc_decoder_config& config,
                       std::string& error);

/** Demux and decode the next owned I420 frame. */
mkvc_result read_frame(CpuAv1Decoder::Impl& impl, std::unique_ptr<DecodedFrame>& frame,
                       std::string& error);

/** Destroy codec and packet-reader resources idempotently. */
void destroy(CpuAv1Decoder::Impl& impl) noexcept;

}  // namespace mkvc::cpu_av1_decoder_detail
