#include "gpu/nvidia/nvdec_packet_pump.hpp"

#include <ffnvcodec/dynlink_cuda.h>
#include <ffnvcodec/dynlink_cuviddec.h>
#include <ffnvcodec/dynlink_nvcuvid.h>

#include <utility>

#include "gpu/nvidia/cuda_context_guard.hpp"
#include "gpu/nvidia/nvdec_callbacks.hpp"
#include "gpu/nvidia/nvdec_runtime_owner.hpp"
#include "webm_packet_reader.hpp"

namespace mkvc::gpu::nvidia {

std::unique_ptr<NvdecPacketPump> NvdecPacketPump::create(const char* path, uint32_t codec,
                                                         NvdecCallbackState& callbacks,
                                                         std::string& error) {
    auto reader = WebmPacketReader::open(path, codec, error);
    if (!reader) return nullptr;
    return std::unique_ptr<NvdecPacketPump>(new NvdecPacketPump(std::move(reader), callbacks));
}

NvdecPacketPump::NvdecPacketPump(std::unique_ptr<WebmPacketReader> reader,
                                 NvdecCallbackState& callbacks) noexcept
    : reader_(std::move(reader)), callbacks_(&callbacks) {}

NvdecPacketPump::~NvdecPacketPump() = default;

mkvc_result NvdecPacketPump::pump_until_output(bool gpu_output, std::string& error) {
    const auto& runtime = callbacks_->runtime();
    CudaContextGuard context_guard(runtime->context(), runtime->api().context_push,
                                   runtime->api().context_pop);
    if (!context_guard) {
        error = "failed to activate CUDA context";
        return MKVC_ERROR_CODEC;
    }

    mkvc_result result = MKVC_OK;
    while (!callbacks_->output_ready(gpu_output)) {
        CUVIDSOURCEDATAPACKET source{};
        EncodedPacket packet;
        if (!demux_eos_) {
            result = reader_->read(packet, error);
            if (result != MKVC_OK && result != MKVC_END_OF_STREAM) break;
            if (result == MKVC_END_OF_STREAM) {
                demux_eos_ = true;
                result = MKVC_OK;
            }
            if (!demux_eos_) {
                source.flags = CUVID_PKT_TIMESTAMP;
                source.payload = packet.data.data();
                source.payload_size = static_cast<tcu_ulong>(packet.data.size());
                source.timestamp = packet.pts_ns;
                ++packets_submitted_;
            }
        }

        if (demux_eos_ && !parser_drained_) {
            source.flags = CUVID_PKT_ENDOFSTREAM;
            parser_drained_ = true;
        } else if (demux_eos_) {
            if (!callbacks_->displayed_any()) {
                error = callbacks_->no_display_diagnostic(packets_submitted_);
                result = MKVC_ERROR_CODEC;
            } else {
                result = MKVC_END_OF_STREAM;
            }
            break;
        }

        callbacks_->clear_error();
        if (runtime->api().parser_parse(runtime->parser(), &source) != CUDA_SUCCESS) {
            error =
                callbacks_->error().empty() ? "cuvidParseVideoData failed" : callbacks_->error();
            result = MKVC_ERROR_CODEC;
            break;
        }
    }

    if (!context_guard.release() && result == MKVC_OK) {
        error = "failed to release CUDA context";
        result = MKVC_ERROR_CODEC;
    }
    return result;
}

}  // namespace mkvc::gpu::nvidia
