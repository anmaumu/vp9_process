#include "nvidia_webm_decoder.hpp"

#include "nvidia_probe.hpp"
#include "webm_packet_reader.hpp"

#if defined(MKVC_HAS_NVIDIA)
#include <ffnvcodec/dynlink_cuda.h>
#include <ffnvcodec/dynlink_cuviddec.h>
#include <ffnvcodec/dynlink_nvcuvid.h>

#include "gpu/gpu_frame.hpp"
#include "gpu/nvidia/cuda_context_guard.hpp"
#include "gpu/nvidia/nvdec_callbacks.hpp"
#include "gpu/nvidia/nvdec_runtime_owner.hpp"
#include "gpu/nvidia/nvdec_runtime_setup.hpp"
#endif

#include <utility>

namespace mkvc {

struct NvidiaWebmDecoder::Impl {
#if defined(MKVC_HAS_NVIDIA)
    std::unique_ptr<gpu::nvidia::NvdecCallbackState> callbacks;
    uint32_t packets_submitted = 0;
    std::unique_ptr<WebmPacketReader> packet_reader;
    bool demux_eos = false;
    bool parser_drained = false;
#endif
    bool closed = false;
};

NvidiaWebmDecoder::NvidiaWebmDecoder() : impl_(std::make_shared<Impl>()) {}
NvidiaWebmDecoder::~NvidiaWebmDecoder() {
    std::string ignored;
    close(ignored);
}

#if defined(MKVC_HAS_NVIDIA)
namespace {

/**
 * @brief Feed demuxed packets until the selected output queue receives a frame.
 *
 * The helper owns CUDA context activation for parser callbacks and submits one
 * explicit end-of-stream packet. CPU/GPU queue ownership remains with the caller.
 */
mkvc_result pump_parser_until_output(NvidiaWebmDecoder::Impl& state, bool gpu_output,
                                     std::string& error) {
    const auto& runtime = state.callbacks->runtime();
    gpu::nvidia::CudaContextGuard context_guard(runtime->context(), runtime->api().context_push,
                                                runtime->api().context_pop);
    if (!context_guard) {
        error = "failed to activate CUDA context";
        return MKVC_ERROR_CODEC;
    }
    mkvc_result result = MKVC_OK;
    const auto output_ready = [&state, gpu_output] {
        return state.callbacks->output_ready(gpu_output);
    };
    while (!output_ready()) {
        CUVIDSOURCEDATAPACKET source{};
        EncodedPacket packet;
        if (!state.demux_eos) {
            result = state.packet_reader->read(packet, error);
            if (result != MKVC_OK && result != MKVC_END_OF_STREAM) break;
            if (result == MKVC_END_OF_STREAM) {
                state.demux_eos = true;
                result = MKVC_OK;
            }
            if (!state.demux_eos) {
                source.flags = CUVID_PKT_TIMESTAMP;
                source.payload = packet.data.data();
                source.payload_size = static_cast<tcu_ulong>(packet.data.size());
                source.timestamp = packet.pts_ns;
                ++state.packets_submitted;
            }
        }
        if (state.demux_eos && !state.parser_drained) {
            source.flags = CUVID_PKT_ENDOFSTREAM;
            state.parser_drained = true;
        } else if (state.demux_eos) {
            if (!state.callbacks->displayed_any()) {
                error = state.callbacks->no_display_diagnostic(state.packets_submitted);
                result = MKVC_ERROR_CODEC;
            } else {
                result = MKVC_END_OF_STREAM;
            }
            break;
        }
        state.callbacks->clear_error();
        if (runtime->api().parser_parse(runtime->parser(), &source) != CUDA_SUCCESS) {
            error = state.callbacks->error().empty() ? "cuvidParseVideoData failed"
                                                     : state.callbacks->error();
            result = MKVC_ERROR_CODEC;
            break;
        }
    }
    if (!context_guard.release()) {
        if (result == MKVC_OK) {
            error = "failed to release CUDA context";
            result = MKVC_ERROR_CODEC;
        }
    }
    return result;
}

}  // namespace
#endif

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
    state.packet_reader = WebmPacketReader::open(config.input_path_utf8, config.codec, error);
    if (!state.packet_reader) return nullptr;
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
    const mkvc_result result = pump_parser_until_output(state, false, error);
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
    const mkvc_result result = pump_parser_until_output(state, true, error);
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
    if (state.callbacks) state.callbacks->clear_outputs();
    state.closed = true;
    if (state.callbacks && state.callbacks->runtime() &&
        !state.callbacks->runtime()->close(error)) {
        cleanup_failed = true;
    }
    state.packet_reader.reset();
#endif
    impl_->closed = true;
    return cleanup_failed ? MKVC_ERROR_CODEC : MKVC_OK;
}

uint32_t NvidiaWebmDecoder::max_pending_observed() const { return 1; }

}  // namespace mkvc
