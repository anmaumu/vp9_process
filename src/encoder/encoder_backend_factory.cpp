#include "encoder/encoder_backend_factory.hpp"

#include <utility>

#include "cpu_av1_encoder.hpp"
#include "cpu_vp9_encoder.hpp"
#include "encoder/encoder_backend.hpp"
#include "intel_webm_encoder.hpp"
#include "nvidia_webm_encoder.hpp"

namespace mkvc::encoder {

std::unique_ptr<EncoderBackend> create_backend(const mkvc_encoder_config& config,
                                               std::string& error) {
    if (config.backend == MKVC_BACKEND_INTEL) {
        auto encoder = IntelWebmEncoder::create(config, error);
        if (!encoder) return nullptr;
        return std::make_unique<EncoderBackendAdapter<IntelWebmEncoder, true>>(std::move(encoder));
    }
    if (config.backend == MKVC_BACKEND_NVIDIA) {
        auto encoder = NvidiaWebmEncoder::create(config, error);
        if (!encoder) return nullptr;
        return std::make_unique<EncoderBackendAdapter<NvidiaWebmEncoder, true>>(std::move(encoder));
    }
    if (config.codec == MKVC_CODEC_VP9) {
        auto encoder = CpuVp9Encoder::create(config, error);
        if (!encoder) return nullptr;
        return std::make_unique<EncoderBackendAdapter<CpuVp9Encoder, false>>(std::move(encoder));
    }
    if (config.codec == MKVC_CODEC_AV1) {
        auto encoder = CpuAv1Encoder::create(config, error);
        if (!encoder) return nullptr;
        return std::make_unique<EncoderBackendAdapter<CpuAv1Encoder, false>>(std::move(encoder));
    }
    error = "unsupported encoder backend or codec";
    return nullptr;
}

}  // namespace mkvc::encoder
