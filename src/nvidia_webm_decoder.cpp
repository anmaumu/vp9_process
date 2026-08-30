#include "nvidia_webm_decoder.hpp"
#include "nvidia_probe.hpp"
#include "gpu/gpu_frame_pool.hpp"
#include "gpu/nvidia/nvidia_native_handle.hpp"

#if defined(MKVC_HAS_NVIDIA)
#include <ffnvcodec/dynlink_cuda.h>
#include <ffnvcodec/dynlink_cuviddec.h>
#include <ffnvcodec/dynlink_nvcuvid.h>
#if __has_include(<webm/mkvparser/mkvparser.h>)
#include <webm/mkvparser/mkvparser.h>
#include <webm/mkvparser/mkvreader.h>
#else
#include <mkvparser/mkvparser.h>
#include <mkvparser/mkvreader.h>
#endif
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#endif

#include <algorithm>
#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace mkvc {

struct NvidiaWebmDecoder::Impl : public std::enable_shared_from_this<Impl> {
#if defined(MKVC_HAS_NVIDIA)
    class Library {
    public:
        explicit Library(const char* name) {
#ifdef _WIN32
            handle_ = LoadLibraryA(name);
#else
            handle_ = dlopen(name, RTLD_LAZY | RTLD_LOCAL);
#endif
        }
        ~Library() {
#ifdef _WIN32
            if (handle_ != nullptr) FreeLibrary(handle_);
#else
            if (handle_ != nullptr) dlclose(handle_);
#endif
        }
        template <typename T> T symbol(const char* name) const {
#ifdef _WIN32
            const FARPROC address = GetProcAddress(handle_, name);
            static_assert(sizeof(T) == sizeof(address));
            T result = nullptr;
            std::memcpy(&result, &address, sizeof(result));
            return result;
#else
            return reinterpret_cast<T>(dlsym(handle_, name));
#endif
        }
        explicit operator bool() const { return handle_ != nullptr; }
    private:
#ifdef _WIN32
        HMODULE handle_ = nullptr;
#else
        void* handle_ = nullptr;
#endif
    };
    struct Packet { std::vector<uint8_t> data; int64_t pts_ns = 0; };
    std::unique_ptr<Library> cuda;
    std::unique_ptr<Library> cuvid;
    tcuInit* cu_init = nullptr;
    tcuDeviceGet* device_get = nullptr;
    tcuCtxCreate_v2* context_create = nullptr;
    tcuCtxDestroy_v2* context_destroy = nullptr;
    tcuCtxPushCurrent_v2* context_push = nullptr;
    tcuCtxPopCurrent_v2* context_pop = nullptr;
    tcuMemcpy2D_v2* memcpy_2d = nullptr;
    tcuvidCreateVideoParser* parser_create = nullptr;
    tcuvidParseVideoData* parser_parse = nullptr;
    tcuvidDestroyVideoParser* parser_destroy = nullptr;
    tcuvidCreateDecoder* decoder_create = nullptr;
    tcuvidDestroyDecoder* decoder_destroy = nullptr;
    tcuvidDecodePicture* decode_picture = nullptr;
    tcuvidMapVideoFrame64* map_frame = nullptr;
    tcuvidUnmapVideoFrame64* unmap_frame = nullptr;
    tcuvidGetDecoderCaps* decoder_caps = nullptr;
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
    std::unique_ptr<mkvparser::MkvReader> reader;
    std::unique_ptr<mkvparser::Segment> segment;
    const mkvparser::Cluster* cluster = nullptr;
    const mkvparser::BlockEntry* block_entry = nullptr;
    int block_frame_index = 0;
    long video_track = 0;
    bool demux_eos = false;
    bool parser_drained = false;
#endif
    bool closed = false;
};

NvidiaWebmDecoder::NvidiaWebmDecoder() : impl_(std::make_shared<Impl>()) {}
NvidiaWebmDecoder::~NvidiaWebmDecoder() { std::string ignored; close(ignored); }

#if defined(MKVC_HAS_NVIDIA)
namespace {

bool destroy_deferred_runtime(NvidiaWebmDecoder::Impl& state) {
    if (state.runtime_destroyed || state.context == nullptr ||
        state.outstanding_gpu_frames != 0) return true;
    if (state.context_push(state.context) != CUDA_SUCCESS) return false;
    if (state.decoder != nullptr) (void)state.decoder_destroy(state.decoder);
    state.decoder = nullptr;
    CUcontext popped = nullptr;
    const bool popped_ok = state.context_pop(&popped) == CUDA_SUCCESS &&
                           popped == state.context;
    if (!popped_ok) return false;
    (void)state.context_destroy(state.context);
    state.context = nullptr;
    state.runtime_destroyed = true;
    return true;
}

void release_mapped_frame(const std::shared_ptr<NvidiaWebmDecoder::Impl>& state,
                          unsigned long long device_pointer) noexcept {
    if (!state) return;
    std::lock_guard<std::mutex> lock(state->runtime_mutex);
    bool pushed = state->context != nullptr &&
                  state->context_push(state->context) == CUDA_SUCCESS;
    if (pushed) {
        (void)state->unmap_frame(state->decoder, device_pointer);
        CUcontext popped = nullptr;
        (void)state->context_pop(&popped);
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
    if (state.decoder_caps(&caps) != CUDA_SUCCESS || caps.bIsSupported == 0 ||
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
    if (state.decoder_create(&state.decoder, &info) != CUDA_SUCCESS) {
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
        state.decode_picture(state.decoder, picture) != CUDA_SUCCESS) {
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
    if (state.map_frame(state.decoder, display->picture_index, &device_pointer,
                        &pitch, &processing) != CUDA_SUCCESS) {
        state.callback_error = "cuvidMapVideoFrame failed";
        return 0;
    }
    if (state.output_mode == NvidiaWebmDecoder::Impl::OutputMode::kGpu) {
        mkvc_gpu_frame_desc desc{};
        desc.struct_size = sizeof(desc);
        desc.struct_version = 1;
        desc.backend = MKVC_BACKEND_NVIDIA;
        desc.memory_type = MKVC_GPU_MEMORY_CUDA_POINTER;
        desc.device_id = 0;
        desc.pixel_format = MKVC_PIXEL_FORMAT_NV12;
        desc.width = state.width;
        desc.height = state.height;
        desc.plane_count = 2;
        desc.plane_offsets[1] = static_cast<uint64_t>(pitch) * state.height;
        desc.pitches[0] = pitch;
        desc.pitches[1] = pitch;
        desc.pts = display->timestamp;
        mkvc_gpu_native_handle_desc native{};
        std::string native_error;
        if (gpu::nvidia::make_cuda_handle(
                0, 0, MKVC_GPU_NATIVE_CUDA_POINTER, device_pointer,
                reinterpret_cast<uintptr_t>(state.context), 0, 0,
                native, native_error) != MKVC_OK) {
            (void)state.unmap_frame(state.decoder, device_pointer);
            state.callback_error = std::move(native_error);
            return 0;
        }
        auto ready = std::make_shared<gpu::ManualCompletion>();
        ready->complete();
        gpu::GpuFramePool::Acquisition acquisition;
        auto shared_state = state.shared_from_this();
        const mkvc_result acquired = state.gpu_pool->acquire(
            desc, ready, native,
            [shared_state, device_pointer] {
                release_mapped_frame(shared_state, device_pointer);
            }, acquisition, state.callback_error,
            {gpu::BackendResourceKind::kNvidiaCudaFrame,
             reinterpret_cast<void*>(static_cast<uintptr_t>(device_pointer))});
        if (acquired != MKVC_OK) {
            (void)state.unmap_frame(state.decoder, device_pointer);
            return 0;
        }
        ++state.outstanding_gpu_frames;
        state.completed_gpu.push_back(std::move(acquisition.core));
        return 1;
    }
    auto frame = std::make_unique<DecodedFrame>();
    frame->width = state.width;
    frame->height = state.height;
    frame->pts_ns = display->timestamp;
    const size_t y_size = static_cast<size_t>(state.width) * state.height;
    const size_t chroma_size = y_size / 4;
    frame->pixels.resize(y_size + chroma_size * 2);
    frame->offsets = {0, y_size, y_size + chroma_size};
    frame->strides = {static_cast<int32_t>(state.width),
                      static_cast<int32_t>(state.width / 2),
                      static_cast<int32_t>(state.width / 2)};
    CUDA_MEMCPY2D copy{};
    copy.srcMemoryType = CU_MEMORYTYPE_DEVICE;
    copy.srcDevice = static_cast<CUdeviceptr>(device_pointer);
    copy.srcPitch = pitch;
    copy.dstMemoryType = CU_MEMORYTYPE_HOST;
    copy.dstHost = frame->pixels.data();
    copy.dstPitch = state.width;
    copy.WidthInBytes = state.width;
    copy.Height = state.height;
    bool copied = state.memcpy_2d(&copy) == CUDA_SUCCESS;
    std::vector<uint8_t> uv(y_size / 2);
    if (copied) {
        copy.srcDevice = static_cast<CUdeviceptr>(device_pointer) +
                         static_cast<CUdeviceptr>(pitch) * state.height;
        copy.dstHost = uv.data();
        copy.dstPitch = state.width;
        copy.Height = state.height / 2;
        copied = state.memcpy_2d(&copy) == CUDA_SUCCESS;
    }
    const CUresult unmapped = state.unmap_frame(state.decoder, device_pointer);
    if (!copied || unmapped != CUDA_SUCCESS) {
        state.callback_error = "NVDEC frame readback failed";
        return 0;
    }
    uint8_t* u = frame->pixels.data() + frame->offsets[1];
    uint8_t* v = frame->pixels.data() + frame->offsets[2];
    for (size_t index = 0; index < chroma_size; ++index) {
        u[index] = uv[index * 2];
        v[index] = uv[index * 2 + 1];
    }
    state.completed.push_back(std::move(frame));
    return 1;
}

bool open_container(const mkvc_decoder_config& config,
                    NvidiaWebmDecoder::Impl& state, std::string& error) {
    state.reader = std::make_unique<mkvparser::MkvReader>();
    if (state.reader->Open(config.input_path_utf8) != 0) {
        error = "failed to open NVIDIA Matroska/WebM input";
        return false;
    }
    long long position = 0;
    mkvparser::EBMLHeader header;
    if (header.Parse(state.reader.get(), position) != 0) {
        error = "invalid EBML header";
        return false;
    }
    mkvparser::Segment* raw = nullptr;
    if (mkvparser::Segment::CreateInstance(state.reader.get(), position, raw) != 0 ||
        raw == nullptr) {
        error = "failed to create NVIDIA libwebm parser";
        return false;
    }
    state.segment.reset(raw);
    if (state.segment->Load() < 0) { error = "failed to load WebM segment"; return false; }
    const char* requested = config.codec == MKVC_CODEC_VP9 ? "V_VP9" : "V_AV1";
    const mkvparser::Tracks* tracks = state.segment->GetTracks();
    if (tracks != nullptr) {
        for (unsigned long index = 0; index < tracks->GetTracksCount(); ++index) {
            const mkvparser::Track* track = tracks->GetTrackByIndex(index);
            if (track != nullptr && track->GetType() == mkvparser::Track::kVideo &&
                track->GetCodecId() != nullptr &&
                std::strcmp(track->GetCodecId(), requested) == 0) {
                state.video_track = track->GetNumber();
                break;
            }
        }
    }
    if (state.video_track == 0) { error = "input has no requested VP9/AV1 video track"; return false; }
    state.cluster = state.segment->GetFirst();
    return true;
}

mkvc_result next_packet(NvidiaWebmDecoder::Impl& state,
                        NvidiaWebmDecoder::Impl::Packet& packet,
                        std::string& error) {
    constexpr uint64_t maximum = 256ULL * 1024 * 1024;
    while (state.cluster != nullptr && !state.cluster->EOS()) {
        if (state.block_entry == nullptr) {
            if (state.cluster->GetFirst(state.block_entry) < 0) { error = "NVDEC cluster read failed"; return MKVC_ERROR_IO; }
            state.block_frame_index = 0;
        }
        while (state.block_entry != nullptr && !state.block_entry->EOS()) {
            const mkvparser::Block* block = state.block_entry->GetBlock();
            if (block != nullptr && block->GetTrackNumber() == state.video_track) {
                while (state.block_frame_index < block->GetFrameCount()) {
                    const auto& source = block->GetFrame(state.block_frame_index++);
                    if (source.len <= 0 || static_cast<uint64_t>(source.len) > maximum ||
                        static_cast<uint64_t>(source.len) > std::numeric_limits<size_t>::max()) {
                        error = "invalid NVIDIA encoded frame size"; return MKVC_ERROR_IO;
                    }
                    packet.data.resize(static_cast<size_t>(source.len));
                    if (source.Read(state.reader.get(), packet.data.data()) != 0) { error = "NVDEC packet read failed"; return MKVC_ERROR_IO; }
                    packet.pts_ns = block->GetTime(state.cluster);
                    return MKVC_OK;
                }
            }
            const mkvparser::BlockEntry* next = nullptr;
            if (state.cluster->GetNext(state.block_entry, next) < 0) { error = "NVDEC block advance failed"; return MKVC_ERROR_IO; }
            state.block_entry = next;
            state.block_frame_index = 0;
        }
        state.cluster = state.segment->GetNext(state.cluster);
        state.block_entry = nullptr;
    }
    state.demux_eos = true;
    return MKVC_END_OF_STREAM;
}

}  // namespace
#endif

std::unique_ptr<NvidiaWebmDecoder> NvidiaWebmDecoder::create(
    const mkvc_decoder_config& config, std::string& error) {
#if !defined(MKVC_HAS_NVIDIA)
    (void)config; error = "NVIDIA backend was not built"; return nullptr;
#else
    const NvidiaProbeResult capability = probe_nvidia();
    const bool supported = config.codec == MKVC_CODEC_VP9
        ? capability.vp9_decode : capability.av1_decode;
    if (!capability.runtime_available || !supported) {
        error = config.codec == MKVC_CODEC_VP9
            ? "NVIDIA VP9 decode is unavailable"
            : "NVIDIA AV1 decode is unavailable";
        return nullptr;
    }
    auto result = std::unique_ptr<NvidiaWebmDecoder>(new NvidiaWebmDecoder());
    auto& state = *result->impl_;
    state.gpu_pool = std::make_shared<gpu::GpuFramePool>(8);
#ifdef _WIN32
    state.cuda = std::make_unique<Impl::Library>("nvcuda.dll");
    state.cuvid = std::make_unique<Impl::Library>("nvcuvid.dll");
#else
    state.cuda = std::make_unique<Impl::Library>("libcuda.so.1");
    state.cuvid = std::make_unique<Impl::Library>("libnvcuvid.so.1");
#endif
    if (!*state.cuda || !*state.cuvid) { error = "NVIDIA CUDA/NVCUVID driver libraries not found"; return nullptr; }
#define MKVC_LOAD(member, library, type, symbol_name) state.member = state.library->symbol<type*>(symbol_name)
    MKVC_LOAD(cu_init, cuda, tcuInit, "cuInit");
    MKVC_LOAD(device_get, cuda, tcuDeviceGet, "cuDeviceGet");
    MKVC_LOAD(context_create, cuda, tcuCtxCreate_v2, "cuCtxCreate_v2");
    MKVC_LOAD(context_destroy, cuda, tcuCtxDestroy_v2, "cuCtxDestroy_v2");
    MKVC_LOAD(context_push, cuda, tcuCtxPushCurrent_v2, "cuCtxPushCurrent_v2");
    MKVC_LOAD(context_pop, cuda, tcuCtxPopCurrent_v2, "cuCtxPopCurrent_v2");
    MKVC_LOAD(memcpy_2d, cuda, tcuMemcpy2D_v2, "cuMemcpy2D_v2");
    MKVC_LOAD(parser_create, cuvid, tcuvidCreateVideoParser, "cuvidCreateVideoParser");
    MKVC_LOAD(parser_parse, cuvid, tcuvidParseVideoData, "cuvidParseVideoData");
    MKVC_LOAD(parser_destroy, cuvid, tcuvidDestroyVideoParser, "cuvidDestroyVideoParser");
    MKVC_LOAD(decoder_create, cuvid, tcuvidCreateDecoder, "cuvidCreateDecoder");
    MKVC_LOAD(decoder_destroy, cuvid, tcuvidDestroyDecoder, "cuvidDestroyDecoder");
    MKVC_LOAD(decode_picture, cuvid, tcuvidDecodePicture, "cuvidDecodePicture");
    MKVC_LOAD(map_frame, cuvid, tcuvidMapVideoFrame64, "cuvidMapVideoFrame64");
    MKVC_LOAD(unmap_frame, cuvid, tcuvidUnmapVideoFrame64, "cuvidUnmapVideoFrame64");
    MKVC_LOAD(decoder_caps, cuvid, tcuvidGetDecoderCaps, "cuvidGetDecoderCaps");
#undef MKVC_LOAD
    if (state.cu_init == nullptr || state.device_get == nullptr || state.context_create == nullptr ||
        state.context_destroy == nullptr || state.context_push == nullptr || state.context_pop == nullptr ||
        state.memcpy_2d == nullptr || state.parser_create == nullptr || state.parser_parse == nullptr ||
        state.parser_destroy == nullptr || state.decoder_create == nullptr || state.decoder_destroy == nullptr ||
        state.decode_picture == nullptr || state.map_frame == nullptr || state.unmap_frame == nullptr ||
        state.decoder_caps == nullptr) {
        error = "NVIDIA driver is missing required NVDEC symbols"; return nullptr;
    }
    CUdevice device = 0;
    if (state.cu_init(0) != CUDA_SUCCESS || state.device_get(&device, 0) != CUDA_SUCCESS ||
        state.context_create(&state.context, 0, device) != CUDA_SUCCESS) {
        error = "failed to create NVIDIA CUDA context"; return nullptr;
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
    if (state.parser_create(&state.parser, &params) != CUDA_SUCCESS) {
        error = "cuvidCreateVideoParser failed"; return nullptr;
    }
    CUcontext popped = nullptr;
    if (state.context_pop(&popped) != CUDA_SUCCESS || popped != state.context) {
        error = "failed to detach NVIDIA CUDA context"; return nullptr;
    }
    state.context_is_current = false;
    if (!open_container(config, state, error)) return nullptr;
    return result;
#endif
}

mkvc_result NvidiaWebmDecoder::read(std::unique_ptr<DecodedFrame>& frame,
                                    std::string& error) {
    frame.reset();
#if !defined(MKVC_HAS_NVIDIA)
    error = "NVIDIA backend was not built"; return MKVC_ERROR_NOT_SUPPORTED;
#else
    auto& state = *impl_;
    if (state.closed) { error = "NVIDIA decoder is closed"; return MKVC_ERROR_INVALID_STATE; }
    if (state.output_mode == Impl::OutputMode::kGpu) {
        error = "NVIDIA decoder output mode cannot switch from GPU to CPU";
        return MKVC_ERROR_INVALID_STATE;
    }
    state.output_mode = Impl::OutputMode::kCpu;
    if (state.context_push(state.context) != CUDA_SUCCESS) { error = "failed to activate CUDA context"; return MKVC_ERROR_CODEC; }
    state.context_is_current = true;
    mkvc_result result = MKVC_OK;
    while (state.completed.empty()) {
        CUVIDSOURCEDATAPACKET source{};
        Impl::Packet packet;
        if (!state.demux_eos) {
            result = next_packet(state, packet, error);
            if (result != MKVC_OK && result != MKVC_END_OF_STREAM) break;
            if (result == MKVC_END_OF_STREAM) result = MKVC_OK;
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
                        std::to_string(state.sequence_callbacks) + " decode=" +
                        std::to_string(state.decode_callbacks) + " display=0 packets=" +
                        std::to_string(state.packets_submitted);
                result = MKVC_ERROR_CODEC;
                break;
            }
            result = MKVC_END_OF_STREAM;
            break;
        }
        state.callback_error.clear();
        if (state.parser_parse(state.parser, &source) != CUDA_SUCCESS) {
            error = state.callback_error.empty() ? "cuvidParseVideoData failed" : state.callback_error;
            result = MKVC_ERROR_CODEC;
            break;
        }
    }
    CUcontext popped = nullptr;
    if (state.context_pop(&popped) != CUDA_SUCCESS || popped != state.context) {
        if (result == MKVC_OK) { error = "failed to release CUDA context"; result = MKVC_ERROR_CODEC; }
    } else state.context_is_current = false;
    if (result == MKVC_OK && !state.completed.empty()) {
        frame = std::move(state.completed.front()); state.completed.pop_front();
    }
    return result;
#endif
}

mkvc_result NvidiaWebmDecoder::read_gpu(mkvc_gpu_frame** frame,
                                        std::string& error) {
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
    if (state.context_push(state.context) != CUDA_SUCCESS) {
        error = "failed to activate CUDA context";
        return MKVC_ERROR_CODEC;
    }
    state.context_is_current = true;
    mkvc_result result = MKVC_OK;
    while (state.completed_gpu.empty()) {
        CUVIDSOURCEDATAPACKET source{};
        Impl::Packet packet;
        if (!state.demux_eos) {
            result = next_packet(state, packet, error);
            if (result != MKVC_OK && result != MKVC_END_OF_STREAM) break;
            if (result == MKVC_END_OF_STREAM) result = MKVC_OK;
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
            result = state.display_callbacks == 0
                ? MKVC_ERROR_CODEC : MKVC_END_OF_STREAM;
            if (result == MKVC_ERROR_CODEC)
                error = "NVDEC parser produced no display callbacks";
            break;
        }
        state.callback_error.clear();
        if (state.parser_parse(state.parser, &source) != CUDA_SUCCESS) {
            error = state.callback_error.empty()
                ? "cuvidParseVideoData failed" : state.callback_error;
            result = MKVC_ERROR_CODEC;
            break;
        }
    }
    CUcontext popped = nullptr;
    if (state.context_pop(&popped) != CUDA_SUCCESS || popped != state.context) {
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
            if (state.context_push(state.context) != CUDA_SUCCESS) {
                error = "failed to activate CUDA context during close";
                cleanup_failed = true;
            } else {
                (void)state.parser_destroy(state.parser);
                state.parser = nullptr;
                state.parser_destroyed = true;
                CUcontext popped = nullptr;
                if (state.context_pop(&popped) != CUDA_SUCCESS ||
                    popped != state.context) cleanup_failed = true;
            }
        }
        state.closed = true;
        if (state.outstanding_gpu_frames == 0 &&
            !destroy_deferred_runtime(state)) {
            error = "failed to destroy deferred NVIDIA runtime";
            cleanup_failed = true;
        }
    }
    state.segment.reset();
    state.reader.reset();
#endif
    impl_->closed = true;
    return cleanup_failed ? MKVC_ERROR_CODEC : MKVC_OK;
}

uint32_t NvidiaWebmDecoder::max_pending_observed() const { return 1; }

}  // namespace mkvc
