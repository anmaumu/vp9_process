#pragma once

#include "cpu_vp9_decoder.hpp"
#include "webm_packet_reader.hpp"

#if defined(MKVC_HAS_CPU_VP9)
#include <vpx/vpx_decoder.h>
#endif

#include <list>
#include <memory>

namespace mkvc {

/** Mutable demux and libvpx state shared by the CPU VP9 decoder modules. */
struct CpuVp9Decoder::Impl {
#if defined(MKVC_HAS_CPU_VP9)
    vpx_codec_ctx_t codec{};
    bool codec_initialized = false;
    vpx_codec_iter_t iterator = nullptr;
    std::unique_ptr<WebmPacketReader> packet_reader;
    std::list<EncodedPacket> submitted_packets;
#endif
    bool demux_eos = false;
    bool drained = false;
    bool closed = false;
};

}  // namespace mkvc
