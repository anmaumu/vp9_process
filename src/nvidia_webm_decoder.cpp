#include "nvidia_webm_decoder.hpp"

#include "nvidia_probe.hpp"

#if defined(MKVC_HAS_NVIDIA)
#include "gpu/gpu_frame.hpp"
#include "gpu/nvidia/nvdec_callbacks.hpp"
#include "gpu/nvidia/nvdec_packet_pump.hpp"
#include "gpu/nvidia/nvdec_runtime_owner.hpp"
#include "gpu/nvidia/nvdec_runtime_setup.hpp"
#endif

#include <utility>

namespace mkvc {

struct NvidiaWebmDecoder::Impl {
#if defined(MKVC_HAS_NVIDIA)
    std::unique_ptr<gpu::nvidia::NvdecCallbackState> callbacks;
    std::unique_ptr<gpu::nvidia::NvdecPacketPump> packet_pump;
#endif
    bool closed = false;
};

NvidiaWebmDecoder::NvidiaWebmDecoder() : impl_(std::make_shared<Impl>()) {}
NvidiaWebmDecoder::~NvidiaWebmDecoder() {
    std::string ignored;
    close(ignored);
}

std::unique_ptr<NvidiaWebmDecoder> NvidiaWebmDecoder::create(const mkvc_decoder_config& config,
                                                             std::string& error) {
#if !defined(MKVC_HAS_NVIDIA)
    (void)config;
    error = "NVIDIA backend was not built";
    return nullptr;
#else
    const NvidiaProbeResult capability = probe_nvidia();
    const bool supported =
        config.codec == MKVC_CODEC_VP9 ? capability.vp9_decode : capability.av1_decode;
    if (!capability.runtime_available || !supported) {
        error = config.codec == MKVC_CODEC_VP9 ? "NVIDIA VP9 decode is unavailable"
                                               : "NVIDIA AV1 decode is unavailable";
        return nullptr;
    }
    auto result = std::unique_ptr<NvidiaWebmDecoder>(new NvidiaWebmDecoder());
    auto& state = *result->impl_;
    state.callbacks = std::make_unique<gpu::nvidia::NvdecCallbackState>(8);
    gpu::nvidia::NvdecParserRuntime runtime;
    if (gpu::nvidia::create_nvdec_parser_runtime(
            static_cast<mkvc_codec>(config.codec), state.callbacks.get(),
            gpu::nvidia::nvdec_sequence_callback, gpu::nvidia::nvdec_decode_callback,
            gpu::nvidia::nvdec_display_callback, runtime, error) != MKVC_OK) {
        return nullptr;
    }
    state.callbacks->attach_runtime(
        std::make_shared<gpu::nvidia::NvdecRuntimeOwner>(std::move(runtime)));
    state.packet_pump = gpu::nvidia::NvdecPacketPump::create(config.input_path_utf8, config.codec,
                                                             *state.callbacks, error);
    if (!state.packet_pump) return nullptr;
    return result;
#endif
}

mkvc_result NvidiaWebmDecoder::read(std::unique_ptr<DecodedFrame>& frame, std::string& error) {
    frame.reset();
#if !defined(MKVC_HAS_NVIDIA)
    error = "NVIDIA backend was not built";
    return MKVC_ERROR_NOT_SUPPORTED;
#else
    auto& state = *impl_;
    if (state.closed) {
        error = "NVIDIA decoder is closed";
        return MKVC_ERROR_INVALID_STATE;
    }
    const mkvc_result selected = state.callbacks->select_cpu(error);
    if (selected != MKVC_OK) return selected;
    const mkvc_result result = state.packet_pump->pump_until_output(false, error);
    if (result == MKVC_OK) frame = state.callbacks->pop_cpu();
    return result;
#endif
}

mkvc_result NvidiaWebmDecoder::read_gpu(mkvc_gpu_frame** frame, std::string& error) {
    if (frame != nullptr) *frame = nullptr;
#if !defined(MKVC_HAS_NVIDIA)
    (void)frame;
    error = "NVIDIA backend was not built";
    return MKVC_ERROR_NOT_SUPPORTED;
#else
    if (frame == nullptr) {
        error = "GPU frame output is null";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    auto& state = *impl_;
    if (state.closed) {
        error = "NVIDIA decoder is closed";
        return MKVC_ERROR_INVALID_STATE;
    }
    const mkvc_result selected = state.callbacks->select_gpu(error);
    if (selected != MKVC_OK) return selected;
    const mkvc_result result = state.packet_pump->pump_until_output(true, error);
    if (result == MKVC_OK) {
        auto core = state.callbacks->pop_gpu();
        if (!core) return MKVC_ERROR_INTERNAL;
        *frame = gpu::make_handle(core);
        if (*frame == nullptr) return MKVC_ERROR_INTERNAL;
    }
    return result;
#endif
}

mkvc_result NvidiaWebmDecoder::close(std::string& error) {
    if (impl_->closed) return MKVC_OK;
    bool cleanup_failed = false;
#if !defined(MKVC_HAS_NVIDIA)
    (void)error;
#else
    auto& state = *impl_;
    state.packet_pump.reset();
    if (state.callbacks) state.callbacks->clear_outputs();
    state.closed = true;
    if (state.callbacks && state.callbacks->runtime() &&
        !state.callbacks->runtime()->close(error)) {
        cleanup_failed = true;
    }
#endif
    impl_->closed = true;
    return cleanup_failed ? MKVC_ERROR_CODEC : MKVC_OK;
}

uint32_t NvidiaWebmDecoder::max_pending_observed() const { return 1; }

}  // namespace mkvc
