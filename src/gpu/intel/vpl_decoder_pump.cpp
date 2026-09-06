#include "gpu/intel/vpl_decoder_pump.hpp"

#include <limits>
#include <utility>

#include "cpu_vp9_decoder.hpp"
#include "gpu/gpu_frame.hpp"
#include "gpu/intel/vpl_decoder_queue.hpp"
#include "gpu/intel/vpl_session.hpp"

namespace mkvc::gpu::intel {
namespace {

mfxU32 vpl_codec(uint32_t codec) { return codec == MKVC_CODEC_VP9 ? MFX_CODEC_VP9 : MFX_CODEC_AV1; }

template <typename Frames, typename Collector>
mkvc_result pump_packet(mfxBitstream& bitstream, VplDecoderQueue& queue, Frames& frames,
                        Collector collect, std::string& error) {
    while (bitstream.DataLength > 0) {
        const mfxU32 before = bitstream.DataLength;
        mkvc_result result = queue.submit(&bitstream, error);
        if (result == MKVC_WOULD_BLOCK && queue.pending_count() != 0) {
            result = collect(frames, error);
            if (result == MKVC_OK) continue;
        }
        if (result != MKVC_OK && result != MKVC_END_OF_STREAM) return result;
        if (result == MKVC_OK && queue.pending_count() >= queue.async_depth()) {
            result = collect(frames, error);
            if (result != MKVC_OK) return result;
        }
        if (bitstream.DataLength >= before) break;
    }
    return MKVC_OK;
}

}  // namespace

VplDecoderPump::VplDecoderPump(uint32_t codec, bool gpu_output, mfxSession session,
                               std::shared_ptr<VplSession> lifetime, VplDecoderQueue& queue)
    : codec_(codec),
      gpu_output_(gpu_output),
      session_(session),
      lifetime_(std::move(lifetime)),
      queue_(queue) {}

mkvc_result VplDecoderPump::prepare(const uint8_t* data, size_t size, int64_t pts,
                                    std::string& error) {
    if (data == nullptr || size == 0 || size > std::numeric_limits<mfxU32>::max()) {
        error = "invalid compressed packet";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    packet_.assign(data, data + size);
    bitstream_ = {};
    bitstream_.Data = packet_.data();
    bitstream_.DataLength = static_cast<mfxU32>(size);
    bitstream_.MaxLength = static_cast<mfxU32>(size);
    bitstream_.TimeStamp = static_cast<mfxU64>(pts);
    return MKVC_OK;
}

mkvc_result VplDecoderPump::initialize(std::string& error) {
    if (initialized_) return MKVC_OK;
    parameters_ = {};
    parameters_.mfx.CodecId = vpl_codec(codec_);
    parameters_.IOPattern =
        gpu_output_ ? MFX_IOPATTERN_OUT_VIDEO_MEMORY : MFX_IOPATTERN_OUT_SYSTEM_MEMORY;
    const mfxStatus header = MFXVideoDECODE_DecodeHeader(session_, &bitstream_, &parameters_);
    if (header != MFX_ERR_NONE) {
        error = "oneVPL DecodeHeader failed with status " + std::to_string(header);
        return header == MFX_ERR_MORE_DATA ? MKVC_ERROR_CODEC : MKVC_ERROR_NOT_SUPPORTED;
    }
    parameters_.AsyncDepth = static_cast<mfxU16>(queue_.async_depth());
    const mfxStatus status = MFXVideoDECODE_Init(session_, &parameters_);
    if (status < MFX_ERR_NONE) {
        error = std::string(gpu_output_ ? "oneVPL video-memory decoder Init failed with status "
                                        : "oneVPL decoder Init failed with status ") +
                std::to_string(status);
        return MKVC_ERROR_CODEC;
    }
    initialized_ = true;
    lifetime_->mark_initialized(VplSession::Component::kDecode);
    return MKVC_OK;
}

mkvc_result VplDecoderPump::decode_cpu(const uint8_t* data, size_t size, int64_t pts,
                                       std::vector<std::unique_ptr<DecodedFrame>>& frames,
                                       std::string& error) {
    mkvc_result result = prepare(data, size, pts, error);
    if (result != MKVC_OK) return result;
    result = initialize(error);
    if (result != MKVC_OK) return result;
    return pump_packet(
        bitstream_, queue_, frames,
        [this](auto& output, auto& diagnostic) { return queue_.collect_cpu(output, diagnostic); },
        error);
}

mkvc_result VplDecoderPump::decode_gpu(const uint8_t* data, size_t size, int64_t pts,
                                       std::vector<std::shared_ptr<GpuFrameCore>>& frames,
                                       std::string& error) {
    mkvc_result result = prepare(data, size, pts, error);
    if (result != MKVC_OK) return result;
    result = initialize(error);
    if (result != MKVC_OK) return result;
    return pump_packet(
        bitstream_, queue_, frames,
        [this](auto& output, auto& diagnostic) { return queue_.collect_gpu(output, diagnostic); },
        error);
}

}  // namespace mkvc::gpu::intel
