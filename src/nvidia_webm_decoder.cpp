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
#endif

#include <algorithm>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace mkvc {

struct NvidiaWebmDecoder::Impl : public std::enable_shared_from_this<Impl> {
#if defined(MKVC_HAS_NVIDIA)
    std::unique_ptr<gpu::nvidia::NvdecApi> api;
    CUcontext context = nullptr;
    CUvideoparser parser = nullptr;
    CUvideodecoder decoder = nullptr;
    bool context_is_current = false;
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
    std::mutex runtime_mutex;
    uint32_t outstanding_gpu_frames = 0;
    bool parser_destroyed = false;
    bool runtime_destroyed = false;
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

bool destroy_deferred_runtime(NvidiaWebmDecoder::Impl& state) {
    if (state.runtime_destroyed || state.context == nullptr || state.outstanding_gpu_frames != 0)
        return true;
    gpu::nvidia::CudaContextGuard context_guard(state.context, state.api->context_push,
                                                state.api->context_pop);
    if (!context_guard) return false;
    if (state.decoder != nullptr) (void)state.api->decoder_destroy(state.decoder);
    state.decoder = nullptr;
    if (!context_guard.release()) return false;
    (void)state.api->context_destroy(state.context);
    state.context = nullptr;
    state.runtime_destroyed = true;
    return true;
}

void release_mapped_frame(const std::shared_ptr<NvidiaWebmDecoder::Impl>& state,
                          unsigned long long device_pointer) noexcept {
    if (!state) return;
    std::lock_guard<std::mutex> lock(state->runtime_mutex);
    gpu::nvidia::CudaContextGuard context_guard(state->context, state->api->context_push,
                                                state->api->context_pop);
    if (context_guard) {
        (void)state->api->unmap_frame(state->decoder, device_pointer);
        (void)context_guard.release();
    }
    if (state->outstanding_gpu_frames != 0) --state->outstanding_gpu_frames;
    if (state->closed && state->outstanding_gpu_frames == 0) {
        (void)destroy_deferred_runtime(*state);
    }
}

int CUDAAPI sequence_callback(void* opaque, CUVIDEOFORMAT* format) {
    auto& state = *static_cast<NvidiaWebmDecoder::Impl*>(opaque);
    ++state.sequence_callbacks;
    if (format == nullptr || format->chroma_format != cudaVideoChromaFormat_420 ||
        format->bit_depth_luma_minus8 != 0 || format->bit_depth_chroma_minus8 != 0 ||
        format->display_area.left != 0 || format->display_area.top != 0) {
        state.callback_error = "NVDEC supports only uncropped 8-bit 4:2:0 input";
        return 0;
    }
    const int width = format->display_area.right;
    const int height = format->display_area.bottom;
    if (width <= 0 || height <= 0 || (width & 1) != 0 || (height & 1) != 0) {
        state.callback_error = "NVDEC returned invalid display dimensions";
        return 0;
    }
    CUVIDDECODECAPS caps{};
    caps.eCodecType = format->codec;
    caps.eChromaFormat = format->chroma_format;
    caps.nBitDepthMinus8 = format->bit_depth_luma_minus8;
    const uint64_t macroblocks =
        (static_cast<uint64_t>(format->coded_width) * format->coded_height + 255) / 256;
    if (state.api->decoder_caps(&caps) != CUDA_SUCCESS || caps.bIsSupported == 0 ||
        format->coded_width < caps.nMinWidth || format->coded_width > caps.nMaxWidth ||
        format->coded_height < caps.nMinHeight || format->coded_height > caps.nMaxHeight ||
        macroblocks > caps.nMaxMBCount) {
        state.callback_error = "NVDEC codec, bit depth, or dimensions are unsupported";
        return 0;
    }
    if (state.decoder != nullptr) {
        if (state.width == static_cast<uint32_t>(width) &&
            state.height == static_cast<uint32_t>(height))
            return std::max(1, static_cast<int>(format->min_num_decode_surfaces));
        state.callback_error = "NVDEC mid-stream resolution change is not supported";
        return 0;
    }
    CUVIDDECODECREATEINFO info{};
    info.ulWidth = format->coded_width;
    info.ulHeight = format->coded_height;
    info.ulNumDecodeSurfaces = format->min_num_decode_surfaces;
    info.CodecType = format->codec;
    info.ChromaFormat = format->chroma_format;
    info.ulCreationFlags = cudaVideoCreate_PreferCUVID;
    info.bitDepthMinus8 = format->bit_depth_luma_minus8;
    info.ulMaxWidth = format->coded_width;
    info.ulMaxHeight = format->coded_height;
    info.display_area.left = static_cast<short>(format->display_area.left);
    info.display_area.top = static_cast<short>(format->display_area.top);
    info.display_area.right = static_cast<short>(format->display_area.right);
    info.display_area.bottom = static_cast<short>(format->display_area.bottom);
    info.OutputFormat = cudaVideoSurfaceFormat_NV12;
    info.DeinterlaceMode = cudaVideoDeinterlaceMode_Weave;
    info.ulTargetWidth = static_cast<tcu_ulong>(width);
    info.ulTargetHeight = static_cast<tcu_ulong>(height);
    info.ulNumOutputSurfaces = 8;
    if (state.api->decoder_create(&state.decoder, &info) != CUDA_SUCCESS) {
        state.callback_error = "cuvidCreateDecoder failed";
        return 0;
    }
    state.width = static_cast<uint32_t>(width);
    state.height = static_cast<uint32_t>(height);
    return std::max(1, static_cast<int>(format->min_num_decode_surfaces));
}

int CUDAAPI decode_callback(void* opaque, CUVIDPICPARAMS* picture) {
    auto& state = *static_cast<NvidiaWebmDecoder::Impl*>(opaque);
    ++state.decode_callbacks;
    if (state.decoder == nullptr || picture == nullptr ||
        state.api->decode_picture(state.decoder, picture) != CUDA_SUCCESS) {
        state.callback_error = "cuvidDecodePicture failed";
        return 0;
    }
    return 1;
}

int CUDAAPI display_callback(void* opaque, CUVIDPARSERDISPINFO* display) {
    auto& state = *static_cast<NvidiaWebmDecoder::Impl*>(opaque);
    ++state.display_callbacks;
    if (state.decoder == nullptr || display == nullptr) {
        state.callback_error = "invalid NVDEC display callback";
        return 0;
    }
    CUVIDPROCPARAMS processing{};
    processing.progressive_frame = display->progressive_frame;
    processing.top_field_first = display->top_field_first;
    processing.unpaired_field = display->repeat_first_field < 0;
    unsigned long long device_pointer = 0;
    unsigned int pitch = 0;
    if (state.api->map_frame(state.decoder, display->picture_index, &device_pointer, &pitch,
                             &processing) != CUDA_SUCCESS) {
        state.callback_error = "cuvidMapVideoFrame failed";
        return 0;
    }
    if (state.output_mode == NvidiaWebmDecoder::Impl::OutputMode::kGpu) {
        auto shared_state = state.shared_from_this();
        std::shared_ptr<gpu::GpuFrameCore> frame;
        const mkvc_result acquired = gpu::nvidia::acquire_nvdec_gpu_frame(
            *state.api, state.decoder, state.context, device_pointer, pitch, state.width,
            state.height, display->timestamp, state.gpu_pool,
            [shared_state, device_pointer] { release_mapped_frame(shared_state, device_pointer); },
            frame, state.callback_error);
        if (acquired != MKVC_OK) {
            return 0;
        }
        ++state.outstanding_gpu_frames;
        state.completed_gpu.push_back(std::move(frame));
        return 1;
    }
    std::unique_ptr<DecodedFrame> frame;
    if (gpu::nvidia::consume_nvdec_cpu_frame(*state.api, state.decoder, device_pointer, pitch,
                                             state.width, state.height, display->timestamp, frame,
                                             state.callback_error) != MKVC_OK) {
        return 0;
    }
    state.completed.push_back(std::move(frame));
    return 1;
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
    state.api = gpu::nvidia::NvdecApi::load(error);
    if (!state.api) return nullptr;
    CUdevice device = 0;
    if (state.api->cu_init(0) != CUDA_SUCCESS ||
        state.api->device_get(&device, 0) != CUDA_SUCCESS ||
        state.api->context_create(&state.context, 0, device) != CUDA_SUCCESS) {
        error = "failed to create NVIDIA CUDA context";
        return nullptr;
    }
    state.context_is_current = true;
    CUVIDPARSERPARAMS params{};
    params.CodecType = config.codec == MKVC_CODEC_VP9 ? cudaVideoCodec_VP9 : cudaVideoCodec_AV1;
    params.ulMaxNumDecodeSurfaces = 20;
    params.ulClockRate = 1000000000U;
    params.ulMaxDisplayDelay = 2;
    params.pUserData = &state;
    params.pfnSequenceCallback = sequence_callback;
    params.pfnDecodePicture = decode_callback;
    params.pfnDisplayPicture = display_callback;
    if (state.api->parser_create(&state.parser, &params) != CUDA_SUCCESS) {
        error = "cuvidCreateVideoParser failed";
        return nullptr;
    }
    CUcontext popped = nullptr;
    if (state.api->context_pop(&popped) != CUDA_SUCCESS || popped != state.context) {
        error = "failed to detach NVIDIA CUDA context";
        return nullptr;
    }
    state.context_is_current = false;
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
    gpu::nvidia::CudaContextGuard context_guard(state.context, state.api->context_push,
                                                state.api->context_pop);
    if (!context_guard) {
        error = "failed to activate CUDA context";
        return MKVC_ERROR_CODEC;
    }
    state.context_is_current = true;
    mkvc_result result = MKVC_OK;
    while (state.completed.empty()) {
        CUVIDSOURCEDATAPACKET source{};
        EncodedPacket packet;
        if (!state.demux_eos) {
            result = state.packet_reader->read(packet, error);
            if (result != MKVC_OK && result != MKVC_END_OF_STREAM) break;
            if (result == MKVC_END_OF_STREAM) {
                state.demux_eos = true;
                result = MKVC_OK;
            }
            if (result == MKVC_OK) {
                if (!state.demux_eos) {
                    source.flags = CUVID_PKT_TIMESTAMP;
                    source.payload = packet.data.data();
                    source.payload_size = static_cast<tcu_ulong>(packet.data.size());
                    source.timestamp = packet.pts_ns;
                    ++state.packets_submitted;
                }
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
                break;
            }
            result = MKVC_END_OF_STREAM;
            break;
        }
        state.callback_error.clear();
        if (state.api->parser_parse(state.parser, &source) != CUDA_SUCCESS) {
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
    } else
        state.context_is_current = false;
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
    gpu::nvidia::CudaContextGuard context_guard(state.context, state.api->context_push,
                                                state.api->context_pop);
    if (!context_guard) {
        error = "failed to activate CUDA context";
        return MKVC_ERROR_CODEC;
    }
    state.context_is_current = true;
    mkvc_result result = MKVC_OK;
    while (state.completed_gpu.empty()) {
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
            result = state.display_callbacks == 0 ? MKVC_ERROR_CODEC : MKVC_END_OF_STREAM;
            if (result == MKVC_ERROR_CODEC) error = "NVDEC parser produced no display callbacks";
            break;
        }
        state.callback_error.clear();
        if (state.api->parser_parse(state.parser, &source) != CUDA_SUCCESS) {
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
    } else {
        state.context_is_current = false;
    }
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
    {
        std::lock_guard<std::mutex> lock(state.runtime_mutex);
        if (state.context != nullptr && state.parser != nullptr) {
            gpu::nvidia::CudaContextGuard context_guard(state.context, state.api->context_push,
                                                        state.api->context_pop);
            if (!context_guard) {
                error = "failed to activate CUDA context during close";
                cleanup_failed = true;
            } else {
                (void)state.api->parser_destroy(state.parser);
                state.parser = nullptr;
                state.parser_destroyed = true;
                if (!context_guard.release()) cleanup_failed = true;
            }
        }
        state.closed = true;
        if (state.outstanding_gpu_frames == 0 && !destroy_deferred_runtime(state)) {
            error = "failed to destroy deferred NVIDIA runtime";
            cleanup_failed = true;
        }
    }
    state.packet_reader.reset();
#endif
    impl_->closed = true;
    return cleanup_failed ? MKVC_ERROR_CODEC : MKVC_OK;
}

uint32_t NvidiaWebmDecoder::max_pending_observed() const { return 1; }

}  // namespace mkvc
