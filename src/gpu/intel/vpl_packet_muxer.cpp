#include "gpu/intel/vpl_packet_muxer.hpp"

#include "intel_vpl_encoder.hpp"
#include "webm_muxer.hpp"

namespace mkvc::gpu::intel {

std::unique_ptr<VplPacketMuxer> VplPacketMuxer::create(const mkvc_encoder_config& config,
                                                       std::string& error) {
    auto result = std::unique_ptr<VplPacketMuxer>(new VplPacketMuxer());
    result->muxer_ = WebmMuxer::create(config.output_path_utf8, config.codec, config.width,
                                       config.height, config.fps_num, config.fps_den, error);
    if (!result->muxer_) return nullptr;
    result->fps_num_ = config.fps_num;
    result->fps_den_ = config.fps_den;
    return result;
}

VplPacketMuxer::~VplPacketMuxer() = default;

mkvc_result VplPacketMuxer::write(const std::vector<IntelEncodedPacket>& packets,
                                  std::string& error) {
    const uint64_t duration_ns = static_cast<uint64_t>(fps_den_) * 1000000000ULL / fps_num_;
    for (const auto& packet : packets) {
        const uint64_t timestamp_ns =
            static_cast<uint64_t>(packet.pts) * fps_den_ * 1000000000ULL / fps_num_;
        const mkvc_result result = muxer_->add_frame(packet.data.data(), packet.data.size(),
                                                     timestamp_ns, duration_ns, packet.key, error);
        if (result != MKVC_OK) return result;
    }
    return MKVC_OK;
}

mkvc_result VplPacketMuxer::finalize(std::string& error) { return muxer_->finalize(error); }

}  // namespace mkvc::gpu::intel
