#pragma once

#include <cstdint>
#include <string>

#include "mkvcodec/mkvc.h"

namespace mkvc {

class WebmMuxer;

namespace gpu::nvidia {

class NvencApi;
struct NvencSession;

/**
 * @brief Lock the completed NVENC bitstream and add it to the output container.
 *
 * @param api Loaded NVENC function table.
 * @param session Initialized synchronous NVENC session.
 * @param muxer Destination WebM/Matroska muxer.
 * @param default_duration_ns Duration used when NVENC reports zero.
 * @param error Receives driver or muxer diagnostics.
 * @return MKVC_OK, MKVC_ERROR_CODEC, or the muxer error.
 *
 * The bitstream remains locked only while `WebmMuxer::add_frame` copies it. Every
 * successful lock is therefore paired with an unlock without an intermediate
 * packet allocation or copy in this adapter.
 */
mkvc_result mux_nvenc_packet(NvencApi& api, const NvencSession& session, WebmMuxer& muxer,
                             uint64_t default_duration_ns, std::string& error);

/**
 * @brief Submit the end-of-stream marker for a synchronous NVENC session.
 * @param api Loaded NVENC function table.
 * @param session Initialized session to drain.
 * @param error Receives the driver diagnostic.
 * @return MKVC_OK or MKVC_ERROR_CODEC.
 */
mkvc_result drain_nvenc_session(NvencApi& api, const NvencSession& session, std::string& error);

}  // namespace gpu::nvidia
}  // namespace mkvc
