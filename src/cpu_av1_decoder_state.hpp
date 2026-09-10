#pragma once

#include "cpu_av1_decoder.hpp"
#include "webm_packet_reader.hpp"

#if defined(MKVC_HAS_CPU_AV1)
#include <aom/aom_decoder.h>
#endif

#include <list>
#include <memory>

namespace mkvc {

/** Mutable demux and libaom state shared by the CPU AV1 decoder modules. */
struct CpuAv1Decoder::Impl {
#if defined(MKVC_HAS_CPU_AV1)
    aom_codec_ctx_t codec{};
    bool codec_initialized = false;
    aom_codec_iter_t iterator = nullptr;
    std::unique_ptr<WebmPacketReader> packet_reader;
    std::list<EncodedPacket> submitted_packets;
    bool output_pending = false;
    const int64_t* active_pts = nullptr;
#endif
    bool demux_eos = false;
    bool closed = false;
};

}  // namespace mkvc
