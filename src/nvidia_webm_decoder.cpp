#include "nvidia_webm_decoder.hpp"

#include "gpu/gpu_frame_pool.hpp"
#include "nvidia_probe.hpp"
#include "webm_packet_reader.hpp"

#if defined(MKVC_HAS_NVIDIA)
#include <ffnvcodec/dynlink_cuda.h>
#include <ffnvcodec/dynlink_cuviddec.h>
#include <ffnvcodec/dynlink_nvcuvid.h>

#include "gpu/nvidia/cuda_context_guard.hpp"
#include "gpu/nvidia/nvdec_api.hpp"
#include "gpu/nvidia/nvdec_cpu_output.hpp"
#include "gpu/nvidia/nvdec_gpu_output.hpp"
#include "gpu/nvidia/nvdec_runtime_owner.hpp"
#include "gpu/nvidia/nvdec_runtime_setup.hpp"
#include "gpu/nvidia/nvdec_sequence.hpp"
#endif

#include <deque>
#include <utility>

namespace mkvc {

struct NvidiaWebmDecoder::Impl {
#if defined(MKVC_HAS_NVIDIA)
    std::shared_ptr<gpu::nvidia::NvdecRuntimeOwner> runtime;
    uint32_t width = 0;
    uint32_t height = 0;
    std::string callback_error;
    uint32_t sequence_callbacks = 0;
    uint32_t decode_callbacks = 0;
    uint32_t display_callbacks = 0;
    uint32_t packets_submitted = 0;
    std::deque<std::unique_ptr<DecodedFrame>> completed;
    std::deque<std::shared_ptr<gpu::GpuFrameCore>> completed_gpu;
    std::shared_ptr<gpu::GpuFramePool> gpu_pool;
    enum class OutputMode { kUnset, kCpu, kGpu } output_mode = OutputMode::kUnset;
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

int CUDAAPI sequence_callback(void* opaque, CUVIDEOFORMAT* format) {
    auto& state = *static_cast<NvidiaWebmDecoder::Impl*>(opaque);
    ++state.sequence_callbacks;
    if (format == nullptr) {
        state.callback_error = "NVDEC sequence callback received no format";
        return 0;
    }
    int decode_surfaces = 0;
    if (gpu::nvidia::configure_nvdec_sequence(state.runtime->api(), *format,
                                              state.runtime->decoder(), state.width, state.height,
                                              decode_surfaces, state.callback_error) != MKVC_OK) {
        return 0;
    }
    return decode_surfaces;
}

int CUDAAPI decode_callback(void* opaque, CUVIDPICPARAMS* picture) {
    auto& state = *static_cast<NvidiaWebmDecoder::Impl*>(opaque);
    ++state.decode_callbacks;
    if (state.runtime->decoder() == nullptr || picture == nullptr ||
        state.runtime->api().decode_picture(state.runtime->decoder(), picture) != CUDA_SUCCESS) {
        state.callback_error = "cuvidDecodePicture failed";
        return 0;
    }
    return 1;
}

int CUDAAPI display_callback(void* opaque, CUVIDPARSERDISPINFO* display) {
    auto& state = *static_cast<NvidiaWebmDecoder::Impl*>(opaque);
    ++state.display_callbacks;
    if (state.runtime->decoder() == nullptr || display == nullptr) {
        state.callback_error = "invalid NVDEC display callback";
        return 0;
    }
    CUVIDPROCPARAMS processing{};
    processing.progressive_frame = display->progressive_frame;
    processing.top_field_first = display->top_field_first;
    processing.unpaired_field = display->repeat_first_field < 0;
    unsigned long long device_pointer = 0;
    unsigned int pitch = 0;
    if (state.runtime->api().map_frame(state.runtime->decoder(), display->picture_index,
                                       &device_pointer, &pitch, &processing) != CUDA_SUCCESS) {
        state.callback_error = "cuvidMapVideoFrame failed";
        return 0;
    }
    if (state.output_mode == NvidiaWebmDecoder::Impl::OutputMode::kGpu) {
        auto runtime = state.runtime;
        std::shared_ptr<gpu::GpuFrameCore> frame;
        const mkvc_result acquired = gpu::nvidia::acquire_nvdec_gpu_frame(
            runtime->api(), runtime->decoder(), runtime->context(), device_pointer, pitch,
            state.width, state.height, display->timestamp, state.gpu_pool,
            [runtime, device_pointer] { runtime->release_mapping(device_pointer); }, frame,
            state.callback_error);
        if (acquired != MKVC_OK) {
            return 0;
        }
        runtime->acquire_mapping();
        state.completed_gpu.push_back(std::move(frame));
        return 1;
    }
    std::unique_ptr<DecodedFrame> frame;
    if (gpu::nvidia::consume_nvdec_cpu_frame(
            state.runtime->api(), state.runtime->decoder(), device_pointer, pitch, state.width,
            state.height, display->timestamp, frame, state.callback_error) != MKVC_OK) {
        return 0;
    }
    state.completed.push_back(std::move(frame));
    return 1;
}

/**
 * @brief Feed demuxed packets until the selected output queue receives a frame.
 *
 * The helper owns CUDA context activation for parser callbacks and submits one
 * explicit end-of-stream packet. CPU/GPU queue ownership remains with the caller.
 */
mkvc_result pump_parser_until_output(NvidiaWebmDecoder::Impl& state, bool gpu_output,
                                     std::string& error) {
    gpu::nvidia::CudaContextGuard context_guard(state.runtime->context(),
                                                state.runtime->api().context_push,
                                                state.runtime->api().context_pop);
    if (!context_guard) {
        error = "failed to activate CUDA context";
        return MKVC_ERROR_CODEC;
    }
    mkvc_result result = MKVC_OK;
    const auto output_ready = [&state, gpu_output] {
        return gpu_output ? !state.completed_gpu.empty() : !state.completed.empty();
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
            if (state.display_callbacks == 0) {
                error = "NVDEC parser produced callbacks sequence=" +
                        std::to_string(state.sequence_callbacks) +
                        " decode=" + std::to_string(state.decode_callbacks) +
                        " display=0 packets=" + std::to_string(state.packets_submitted);
                result = MKVC_ERROR_CODEC;
            } else {
                result = MKVC_END_OF_STREAM;
            }
            break;
        }
        state.callback_error.clear();
        if (state.runtime->api().parser_parse(state.runtime->parser(), &source) != CUDA_SUCCESS) {
            error =
                state.callback_error.empty() ? "cuvidParseVideoData failed" : state.callback_error;
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
    state.gpu_pool = std::make_shared<gpu::GpuFramePool>(8);
    gpu::nvidia::NvdecParserRuntime runtime;
    if (gpu::nvidia::create_nvdec_parser_runtime(static_cast<mkvc_codec>(config.codec), &state,
                                                 sequence_callback, decode_callback,
                                                 display_callback, runtime, error) != MKVC_OK) {
        return nullptr;
    }
    state.runtime = std::make_shared<gpu::nvidia::NvdecRuntimeOwner>(std::move(runtime));
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
    if (state.output_mode == Impl::OutputMode::kGpu) {
        error = "NVIDIA decoder output mode cannot switch from GPU to CPU";
        return MKVC_ERROR_INVALID_STATE;
    }
    state.output_mode = Impl::OutputMode::kCpu;
    const mkvc_result result = pump_parser_until_output(state, false, error);
    if (result == MKVC_OK && !state.completed.empty()) {
        frame = std::move(state.completed.front());
        state.completed.pop_front();
    }
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
    if (state.output_mode == Impl::OutputMode::kCpu) {
        error = "NVIDIA decoder output mode cannot switch from CPU to GPU";
        return MKVC_ERROR_INVALID_STATE;
    }
    state.output_mode = Impl::OutputMode::kGpu;
    if (state.gpu_pool->in_use() >= state.gpu_pool->capacity()) {
        error = "NVIDIA GPU frame pool is full";
        return MKVC_WOULD_BLOCK;
    }
    const mkvc_result result = pump_parser_until_output(state, true, error);
    if (result == MKVC_OK && !state.completed_gpu.empty()) {
        auto core = std::move(state.completed_gpu.front());
        state.completed_gpu.pop_front();
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
    state.completed.clear();
    state.completed_gpu.clear();
    state.closed = true;
    if (state.runtime && !state.runtime->close(error)) cleanup_failed = true;
    state.packet_reader.reset();
#endif
    impl_->closed = true;
    return cleanup_failed ? MKVC_ERROR_CODEC : MKVC_OK;
}

uint32_t NvidiaWebmDecoder::max_pending_observed() const { return 1; }

}  // namespace mkvc
