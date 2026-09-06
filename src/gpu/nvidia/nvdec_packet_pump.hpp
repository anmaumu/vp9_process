/**
 * @file nvdec_packet_pump.hpp
 * @brief Incremental WebM packet submission for an NVDEC parser.
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "mkvcodec/mkvc.h"

namespace mkvc {

class WebmPacketReader;

namespace gpu::nvidia {

class NvdecCallbackState;

/**
 * @brief Feed one compressed stream into NVDEC until an output frame is ready.
 *
 * The pump owns incremental demux state, packet counters, and the one-shot EOS
 * transition. It borrows callback state, which must outlive the pump.
 */
class NvdecPacketPump final {
   public:
    /**
     * @brief Open the input stream and bind it to an initialized callback state.
     * @param path UTF-8 WebM/Matroska path.
     * @param codec Requested VP9 or AV1 codec.
     * @param callbacks Callback state with an attached NVDEC runtime.
     * @param error Receives a diagnostic on failure.
     * @return Owning packet pump, or nullptr on failure.
     */
    static std::unique_ptr<NvdecPacketPump> create(const char* path, uint32_t codec,
                                                   NvdecCallbackState& callbacks,
                                                   std::string& error);

    ~NvdecPacketPump();
    NvdecPacketPump(const NvdecPacketPump&) = delete;
    NvdecPacketPump& operator=(const NvdecPacketPump&) = delete;

    /**
     * @brief Submit packets until the selected CPU or GPU queue receives output.
     * @param gpu_output true for the GPU output queue; false for CPU output.
     * @param error Receives parser, callback, context, or drain diagnostics.
     * @return MKVC_OK, MKVC_END_OF_STREAM, or an error result.
     */
    mkvc_result pump_until_output(bool gpu_output, std::string& error);

   private:
    NvdecPacketPump(std::unique_ptr<WebmPacketReader> reader,
                    NvdecCallbackState& callbacks) noexcept;

    std::unique_ptr<WebmPacketReader> reader_;
    NvdecCallbackState* callbacks_ = nullptr;
    uint32_t packets_submitted_ = 0;
    bool demux_eos_ = false;
    bool parser_drained_ = false;
};

}  // namespace gpu::nvidia
}  // namespace mkvc
