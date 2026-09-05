#include "nvidia_webm_encoder.hpp"

#include "gpu/gpu_frame.hpp"
#include "nvidia_probe.hpp"
#include "webm_muxer.hpp"

#if defined(MKVC_HAS_NVIDIA)
#include <ffnvcodec/dynlink_cuda.h>
#include <ffnvcodec/nvEncodeAPI.h>
#include <libyuv/convert.h>
#include <libyuv/planar_functions.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#endif

#include <algorithm>
#include <cstring>
#include <limits>
#include <vector>

namespace mkvc {

struct NvidiaWebmEncoder::Impl {
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
        template <typename T>
        T symbol(const char* name) const {
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
    std::unique_ptr<Library> cuda;
    std::unique_ptr<Library> nvenc;
    tcuInit* cu_init = nullptr;
    tcuDeviceGet* device_get = nullptr;
    tcuCtxCreate_v2* context_create = nullptr;
    tcuCtxDestroy_v2* context_destroy = nullptr;
    CUcontext context = nullptr;
    CUcontext external_context = nullptr;
    bool owns_context = false;
    NV_ENCODE_API_FUNCTION_LIST functions{};
    void* encoder = nullptr;
    NV_ENC_INPUT_PTR input = nullptr;
    NV_ENC_OUTPUT_PTR output = nullptr;
    std::shared_ptr<gpu::GpuFrameCore> context_anchor;
    std::unique_ptr<WebmMuxer> muxer;
#endif
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t fps_num = 0;
    uint32_t fps_den = 0;
    uint32_t quality = 0;
    uint32_t keyframe_interval = 0;
    uint64_t frame_index = 0;
    int64_t next_pts = 0;
    bool closed = false;
    std::vector<uint8_t> i420;
    std::vector<uint8_t> nv12;
};

NvidiaWebmEncoder::NvidiaWebmEncoder() : impl_(std::make_unique<Impl>()) {}
NvidiaWebmEncoder::~NvidiaWebmEncoder() {
    std::string ignored;
    close(ignored);
}

#if defined(MKVC_HAS_NVIDIA)
namespace {

using CreateInstance = NVENCSTATUS(NVENCAPI*)(NV_ENCODE_API_FUNCTION_LIST*);

void copy_plane(uint8_t* destination, int destination_stride, const uint8_t* source,
                int source_stride, uint32_t width, uint32_t height) {
    for (uint32_t row = 0; row < height; ++row)
        std::memcpy(destination + static_cast<size_t>(row) * destination_stride,
                    source + static_cast<size_t>(row) * source_stride, width);
}

mkvc_result convert_to_nv12(NvidiaWebmEncoder::Impl& state, const mkvc_frame_view& frame,
                            std::string& error) {
    const size_t y_size = static_cast<size_t>(state.width) * state.height;
    const size_t chroma = y_size / 4;
    uint8_t* y = state.i420.data();
    uint8_t* u = y + y_size;
    uint8_t* v = u + chroma;
    uint8_t* nv_y = state.nv12.data();
    uint8_t* nv_uv = nv_y + y_size;
    int converted = 0;
    switch (frame.pixel_format) {
        case MKVC_PIXEL_FORMAT_NV12:
            if (frame.planes[0] == nullptr || frame.planes[1] == nullptr ||
                frame.strides[0] < static_cast<int32_t>(state.width) ||
                frame.strides[1] < static_cast<int32_t>(state.width)) {
                error = "NV12 requires valid Y and UV planes";
                return MKVC_ERROR_INVALID_ARGUMENT;
            }
            copy_plane(nv_y, static_cast<int>(state.width), frame.planes[0], frame.strides[0],
                       state.width, state.height);
            copy_plane(nv_uv, static_cast<int>(state.width), frame.planes[1], frame.strides[1],
                       state.width, state.height / 2);
            return MKVC_OK;
        case MKVC_PIXEL_FORMAT_I420:
            if (frame.planes[0] == nullptr || frame.planes[1] == nullptr ||
                frame.planes[2] == nullptr ||
                frame.strides[0] < static_cast<int32_t>(state.width) ||
                frame.strides[1] < static_cast<int32_t>(state.width / 2) ||
                frame.strides[2] < static_cast<int32_t>(state.width / 2)) {
                error = "I420 requires three valid planes";
                return MKVC_ERROR_INVALID_ARGUMENT;
            }
            converted = libyuv::I420ToNV12(
                frame.planes[0], frame.strides[0], frame.planes[1], frame.strides[1],
                frame.planes[2], frame.strides[2], nv_y, static_cast<int>(state.width), nv_uv,
                static_cast<int>(state.width), static_cast<int>(state.width),
                static_cast<int>(state.height));
            break;
        case MKVC_PIXEL_FORMAT_BGR24:
        case MKVC_PIXEL_FORMAT_RGB24:
        case MKVC_PIXEL_FORMAT_BGRA32: {
            const uint32_t channels = frame.pixel_format == MKVC_PIXEL_FORMAT_BGRA32 ? 4u : 3u;
            if (frame.planes[0] == nullptr ||
                frame.strides[0] < static_cast<int32_t>(state.width * channels)) {
                error = "packed RGB input has an invalid plane or stride";
                return MKVC_ERROR_INVALID_ARGUMENT;
            }
            if (frame.pixel_format == MKVC_PIXEL_FORMAT_BGR24)
                converted = libyuv::RGB24ToI420(frame.planes[0], frame.strides[0], y, state.width,
                                                u, state.width / 2, v, state.width / 2, state.width,
                                                state.height);
            else if (frame.pixel_format == MKVC_PIXEL_FORMAT_RGB24)
                converted = libyuv::RAWToI420(frame.planes[0], frame.strides[0], y, state.width, u,
                                              state.width / 2, v, state.width / 2, state.width,
                                              state.height);
            else
                converted = libyuv::ARGBToI420(frame.planes[0], frame.strides[0], y, state.width, u,
                                               state.width / 2, v, state.width / 2, state.width,
                                               state.height);
            if (converted == 0)
                converted =
                    libyuv::I420ToNV12(y, state.width, u, state.width / 2, v, state.width / 2, nv_y,
                                       state.width, nv_uv, state.width, state.width, state.height);
            break;
        }
        default:
            error = "unsupported NVIDIA encoder input format";
            return MKVC_ERROR_NOT_SUPPORTED;
    }
    if (converted != 0) {
        error = "libyuv failed to convert NVIDIA encoder input";
        return MKVC_ERROR_INTERNAL;
    }
    return MKVC_OK;
}

void destroy_session(NvidiaWebmEncoder::Impl& state) {
    if (state.encoder != nullptr) {
        if (state.input != nullptr)
            (void)state.functions.nvEncDestroyInputBuffer(state.encoder, state.input);
        if (state.output != nullptr)
            (void)state.functions.nvEncDestroyBitstreamBuffer(state.encoder, state.output);
        (void)state.functions.nvEncDestroyEncoder(state.encoder);
    }
    state.input = nullptr;
    state.output = nullptr;
    state.encoder = nullptr;
    if (state.context != nullptr && state.owns_context) (void)state.context_destroy(state.context);
    state.context = nullptr;
    state.owns_context = false;
}

bool initialize_session(NvidiaWebmEncoder::Impl& state, std::string& error) {
    if (state.external_context != nullptr) {
        state.context = state.external_context;
        state.owns_context = false;
    } else {
        CUdevice device = 0;
        if (state.cu_init(0) != CUDA_SUCCESS || state.device_get(&device, 0) != CUDA_SUCCESS ||
            state.context_create(&state.context, 0, device) != CUDA_SUCCESS) {
            error = "failed to create NVIDIA encode CUDA context";
            return false;
        }
        state.owns_context = true;
    }
    NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS open{};
    open.version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
    open.deviceType = NV_ENC_DEVICE_TYPE_CUDA;
    open.device = state.context;
    open.apiVersion = NVENCAPI_VERSION;
    if (state.functions.nvEncOpenEncodeSessionEx(&open, &state.encoder) != NV_ENC_SUCCESS) {
        error = "nvEncOpenEncodeSessionEx failed";
        return false;
    }
    NV_ENC_PRESET_CONFIG preset{};
    preset.version = NV_ENC_PRESET_CONFIG_VER;
    preset.presetCfg.version = NV_ENC_CONFIG_VER;
    if (state.functions.nvEncGetEncodePresetConfigEx(
            state.encoder, NV_ENC_CODEC_AV1_GUID, NV_ENC_PRESET_P4_GUID,
            NV_ENC_TUNING_INFO_HIGH_QUALITY, &preset) != NV_ENC_SUCCESS) {
        error = "NVENC AV1 P4 preset query failed";
        return false;
    }
    NV_ENC_CONFIG config = preset.presetCfg;
    config.version = NV_ENC_CONFIG_VER;
    config.profileGUID = NV_ENC_AV1_PROFILE_MAIN_GUID;
    config.gopLength = state.keyframe_interval;
    config.frameIntervalP = 1;
    config.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CONSTQP;
    config.rcParams.constQP.qpInterP = state.quality;
    config.rcParams.constQP.qpInterB = state.quality;
    config.rcParams.constQP.qpIntra = state.quality;
    NV_ENC_INITIALIZE_PARAMS init{};
    init.version = NV_ENC_INITIALIZE_PARAMS_VER;
    init.encodeGUID = NV_ENC_CODEC_AV1_GUID;
    init.presetGUID = NV_ENC_PRESET_P4_GUID;
    init.encodeWidth = state.width;
    init.encodeHeight = state.height;
    init.darWidth = state.width;
    init.darHeight = state.height;
    init.frameRateNum = state.fps_num;
    init.frameRateDen = state.fps_den;
    init.enableEncodeAsync = 0;
    init.enablePTD = 1;
    init.maxEncodeWidth = state.width;
    init.maxEncodeHeight = state.height;
    init.tuningInfo = NV_ENC_TUNING_INFO_HIGH_QUALITY;
    init.encodeConfig = &config;
    if (state.functions.nvEncInitializeEncoder(state.encoder, &init) != NV_ENC_SUCCESS) {
        error = "nvEncInitializeEncoder AV1 failed";
        return false;
    }
    NV_ENC_CREATE_INPUT_BUFFER input{};
    input.version = NV_ENC_CREATE_INPUT_BUFFER_VER;
    input.width = state.width;
    input.height = state.height;
    input.bufferFmt = NV_ENC_BUFFER_FORMAT_NV12;
    if (state.functions.nvEncCreateInputBuffer(state.encoder, &input) != NV_ENC_SUCCESS) {
        error = "nvEncCreateInputBuffer failed";
        return false;
    }
    state.input = input.inputBuffer;
    NV_ENC_CREATE_BITSTREAM_BUFFER output{};
    output.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;
    if (state.functions.nvEncCreateBitstreamBuffer(state.encoder, &output) != NV_ENC_SUCCESS) {
        error = "nvEncCreateBitstreamBuffer failed";
        return false;
    }
    state.output = output.bitstreamBuffer;
    return true;
}

mkvc_result emit_packet(NvidiaWebmEncoder::Impl& state, std::string& error) {
    NV_ENC_LOCK_BITSTREAM lock{};
    lock.version = NV_ENC_LOCK_BITSTREAM_VER;
    lock.outputBitstream = state.output;
    if (state.functions.nvEncLockBitstream(state.encoder, &lock) != NV_ENC_SUCCESS) {
        error = "nvEncLockBitstream failed";
        return MKVC_ERROR_CODEC;
    }
    const uint64_t default_duration =
        static_cast<uint64_t>(state.fps_den) * 1000000000ULL / state.fps_num;
    const mkvc_result mux_result = state.muxer->add_frame(
        static_cast<const uint8_t*>(lock.bitstreamBufferPtr), lock.bitstreamSizeInBytes,
        lock.outputTimeStamp, lock.outputDuration != 0 ? lock.outputDuration : default_duration,
        lock.pictureType == NV_ENC_PIC_TYPE_IDR || lock.pictureType == NV_ENC_PIC_TYPE_I, error);
    (void)state.functions.nvEncUnlockBitstream(state.encoder, state.output);
    return mux_result;
}

}  // namespace
#endif

std::unique_ptr<NvidiaWebmEncoder> NvidiaWebmEncoder::create(const mkvc_encoder_config& config,
                                                             std::string& error) {
#if !defined(MKVC_HAS_NVIDIA)
    (void)config;
    error = "NVIDIA backend was not built";
    return nullptr;
#else
    if (config.codec != MKVC_CODEC_AV1 || !probe_nvidia().av1_encode) {
        error = "NVIDIA AV1 encode is unavailable";
        return nullptr;
    }
    auto result = std::unique_ptr<NvidiaWebmEncoder>(new NvidiaWebmEncoder());
    auto& state = *result->impl_;
    state.width = config.width;
    state.height = config.height;
    state.fps_num = config.fps_num;
    state.fps_den = config.fps_den;
    state.quality = config.quality;
    state.keyframe_interval = config.keyframe_interval_frames == 0
                                  ? std::max(1u, 4u * config.fps_num / config.fps_den)
                                  : config.keyframe_interval_frames;
#ifdef _WIN32
    state.cuda = std::make_unique<Impl::Library>("nvcuda.dll");
    state.nvenc = std::make_unique<Impl::Library>("nvEncodeAPI64.dll");
#else
    state.cuda = std::make_unique<Impl::Library>("libcuda.so.1");
    state.nvenc = std::make_unique<Impl::Library>("libnvidia-encode.so.1");
#endif
    if (!*state.cuda || !*state.nvenc) {
        error = "NVIDIA encode driver libraries not found";
        return nullptr;
    }
    state.cu_init = state.cuda->symbol<tcuInit*>("cuInit");
    state.device_get = state.cuda->symbol<tcuDeviceGet*>("cuDeviceGet");
    state.context_create = state.cuda->symbol<tcuCtxCreate_v2*>("cuCtxCreate_v2");
    state.context_destroy = state.cuda->symbol<tcuCtxDestroy_v2*>("cuCtxDestroy_v2");
    const auto create_instance = state.nvenc->symbol<CreateInstance>("NvEncodeAPICreateInstance");
    if (state.cu_init == nullptr || state.device_get == nullptr ||
        state.context_create == nullptr || state.context_destroy == nullptr ||
        create_instance == nullptr) {
        error = "NVIDIA encode driver is missing required symbols";
        return nullptr;
    }
    state.functions.version = NV_ENCODE_API_FUNCTION_LIST_VER;
    if (create_instance(&state.functions) != NV_ENC_SUCCESS ||
        state.functions.nvEncGetEncodePresetConfigEx == nullptr) {
        error = "NvEncodeAPICreateInstance failed";
        return nullptr;
    }
    if (!initialize_session(state, error)) return nullptr;
    state.muxer = WebmMuxer::create(config.output_path_utf8, MKVC_CODEC_AV1, config.width,
                                    config.height, config.fps_num, config.fps_den, error);
    if (!state.muxer) return nullptr;
    const uint64_t y_size = static_cast<uint64_t>(config.width) * config.height;
    if (y_size * 3 / 2 > std::numeric_limits<size_t>::max()) {
        error = "NVIDIA frame dimensions exceed memory";
        return nullptr;
    }
    state.i420.resize(static_cast<size_t>(y_size * 3 / 2));
    state.nv12.resize(static_cast<size_t>(y_size * 3 / 2));
    return result;
#endif
}

mkvc_result NvidiaWebmEncoder::write_gpu(const std::shared_ptr<gpu::GpuFrameCore>& frame,
                                         std::string& error) {
#if !defined(MKVC_HAS_NVIDIA)
    (void)frame;
    error = "NVIDIA backend was not built";
    return MKVC_ERROR_NOT_SUPPORTED;
#else
    if (!frame) {
        error = "GPU frame is null";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    auto& state = *impl_;
    if (state.closed) {
        error = "NVIDIA encoder is closed";
        return MKVC_ERROR_INVALID_STATE;
    }
    const mkvc_gpu_frame_desc& desc = frame->desc();
    const bool cuda_pointer = desc.memory_type == MKVC_GPU_MEMORY_CUDA_POINTER;
    const bool cuda_array = desc.memory_type == MKVC_GPU_MEMORY_CUDA_ARRAY;
    if (desc.backend != MKVC_BACKEND_NVIDIA || (!cuda_pointer && !cuda_array) ||
        desc.pixel_format != MKVC_PIXEL_FORMAT_NV12 || desc.plane_count != 2) {
        error = "NVENC requires a NVIDIA CUDA pointer/array NV12 frame";
        return MKVC_ERROR_NOT_SUPPORTED;
    }
    if (desc.width != state.width || desc.height != state.height || desc.pitches[0] < state.width ||
        desc.pitches[0] != desc.pitches[1] ||
        desc.pitches[0] > std::numeric_limits<uint32_t>::max()) {
        error = "GPU frame dimensions or NV12 pitch do not match NVENC";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    const auto producer = frame->producer_completion();
    if (!producer) {
        error = "GPU frame has no producer completion";
        return MKVC_ERROR_INVALID_STATE;
    }
    mkvc_result result = producer->wait(std::numeric_limits<uint32_t>::max(), error);
    if (result != MKVC_OK) return result;
    mkvc_gpu_native_handle_desc native{};
    result = frame->get_native_handle(native, error);
    if (result != MKVC_OK) return result;
    const uint32_t expected_native_type =
        cuda_pointer ? MKVC_GPU_NATIVE_CUDA_POINTER : MKVC_GPU_NATIVE_CUDA_ARRAY;
    if (native.type != expected_native_type || native.handles[0] == 0 || native.handles[1] == 0 ||
        native.handles[0] !=
            static_cast<uint64_t>(reinterpret_cast<uintptr_t>(frame->backend_resource().object))) {
        error = "GPU frame has an inconsistent CUDA native handle";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    const auto source_context =
        reinterpret_cast<CUcontext>(static_cast<uintptr_t>(native.handles[1]));
    if (state.external_context == nullptr) {
        if (state.frame_index != 0) {
            error = "cannot switch a running CPU-input NVENC session to GPU input";
            return MKVC_ERROR_INVALID_STATE;
        }
        destroy_session(state);
        state.external_context = source_context;
        state.context_anchor = frame;
        if (!initialize_session(state, error)) return MKVC_ERROR_CODEC;
    } else if (state.external_context != source_context) {
        error = "all GPU frames must belong to the NVENC session CUDA context";
        return MKVC_ERROR_NOT_SUPPORTED;
    }

    NV_ENC_REGISTER_RESOURCE registration{};
    registration.version = NV_ENC_REGISTER_RESOURCE_VER;
    registration.resourceType = cuda_pointer ? NV_ENC_INPUT_RESOURCE_TYPE_CUDADEVICEPTR
                                             : NV_ENC_INPUT_RESOURCE_TYPE_CUDAARRAY;
    registration.resourceToRegister =
        reinterpret_cast<void*>(static_cast<uintptr_t>(native.handles[0]));
    registration.width = state.width;
    registration.height = state.height;
    registration.pitch = static_cast<uint32_t>(desc.pitches[0]);
    registration.bufferFormat = NV_ENC_BUFFER_FORMAT_NV12;
    registration.bufferUsage = NV_ENC_INPUT_IMAGE;
    if (state.functions.nvEncRegisterResource(state.encoder, &registration) != NV_ENC_SUCCESS) {
        error = "nvEncRegisterResource rejected the CUDA input resource";
        return MKVC_ERROR_CODEC;
    }
    NV_ENC_MAP_INPUT_RESOURCE mapping{};
    mapping.version = NV_ENC_MAP_INPUT_RESOURCE_VER;
    mapping.registeredResource = registration.registeredResource;
    if (state.functions.nvEncMapInputResource(state.encoder, &mapping) != NV_ENC_SUCCESS) {
        (void)state.functions.nvEncUnregisterResource(state.encoder,
                                                      registration.registeredResource);
        error = "nvEncMapInputResource failed";
        return MKVC_ERROR_CODEC;
    }
    const int64_t duration =
        static_cast<int64_t>(static_cast<uint64_t>(state.fps_den) * 1000000000ULL / state.fps_num);
    const int64_t pts = desc.pts >= 0 ? desc.pts : state.next_pts;
    NV_ENC_PIC_PARAMS picture{};
    picture.version = NV_ENC_PIC_PARAMS_VER;
    picture.inputWidth = state.width;
    picture.inputHeight = state.height;
    picture.inputPitch = registration.pitch;
    picture.inputBuffer = mapping.mappedResource;
    picture.outputBitstream = state.output;
    picture.bufferFmt = mapping.mappedBufferFmt;
    picture.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
    picture.frameIdx = static_cast<uint32_t>(state.frame_index);
    picture.inputTimeStamp = static_cast<uint64_t>(pts);
    picture.inputDuration = static_cast<uint64_t>(duration);
    if (state.frame_index % state.keyframe_interval == 0)
        picture.encodePicFlags = NV_ENC_PIC_FLAG_FORCEIDR;
    const NVENCSTATUS encoded = state.functions.nvEncEncodePicture(state.encoder, &picture);
    if (encoded == NV_ENC_SUCCESS)
        result = emit_packet(state, error);
    else {
        error = "nvEncEncodePicture failed for GPU input";
        result = MKVC_ERROR_CODEC;
    }
    (void)state.functions.nvEncUnmapInputResource(state.encoder, mapping.mappedResource);
    (void)state.functions.nvEncUnregisterResource(state.encoder, registration.registeredResource);
    if (result == MKVC_OK) {
        ++state.frame_index;
        state.next_pts = std::max(state.next_pts, pts + duration);
    }
    return result;
#endif
}

mkvc_result NvidiaWebmEncoder::write(const mkvc_frame_view& frame, std::string& error) {
#if !defined(MKVC_HAS_NVIDIA)
    (void)frame;
    error = "NVIDIA backend was not built";
    return MKVC_ERROR_NOT_SUPPORTED;
#else
    auto& state = *impl_;
    if (state.closed) {
        error = "NVIDIA encoder is closed";
        return MKVC_ERROR_INVALID_STATE;
    }
    if (frame.width != state.width || frame.height != state.height) {
        error = "frame dimensions do not match NVIDIA encoder";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    mkvc_result result = convert_to_nv12(state, frame, error);
    if (result != MKVC_OK) return result;
    NV_ENC_LOCK_INPUT_BUFFER lock{};
    lock.version = NV_ENC_LOCK_INPUT_BUFFER_VER;
    lock.inputBuffer = state.input;
    if (state.functions.nvEncLockInputBuffer(state.encoder, &lock) != NV_ENC_SUCCESS) {
        error = "nvEncLockInputBuffer failed";
        return MKVC_ERROR_CODEC;
    }
    const uint8_t* source_y = state.nv12.data();
    const uint8_t* source_uv = source_y + static_cast<size_t>(state.width) * state.height;
    auto* destination = static_cast<uint8_t*>(lock.bufferDataPtr);
    copy_plane(destination, lock.pitch, source_y, state.width, state.width, state.height);
    copy_plane(destination + static_cast<size_t>(lock.pitch) * state.height, lock.pitch, source_uv,
               state.width, state.width, state.height / 2);
    (void)state.functions.nvEncUnlockInputBuffer(state.encoder, state.input);
    const int64_t duration =
        static_cast<int64_t>(static_cast<uint64_t>(state.fps_den) * 1000000000ULL / state.fps_num);
    const int64_t pts = frame.pts >= 0 ? frame.pts : state.next_pts;
    NV_ENC_PIC_PARAMS picture{};
    picture.version = NV_ENC_PIC_PARAMS_VER;
    picture.inputWidth = state.width;
    picture.inputHeight = state.height;
    picture.inputPitch = lock.pitch;
    picture.inputBuffer = state.input;
    picture.outputBitstream = state.output;
    picture.bufferFmt = NV_ENC_BUFFER_FORMAT_NV12;
    picture.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
    picture.frameIdx = static_cast<uint32_t>(state.frame_index);
    picture.inputTimeStamp = static_cast<uint64_t>(pts);
    picture.inputDuration = static_cast<uint64_t>(duration);
    if (state.frame_index % state.keyframe_interval == 0)
        picture.encodePicFlags = NV_ENC_PIC_FLAG_FORCEIDR;
    const NVENCSTATUS encoded = state.functions.nvEncEncodePicture(state.encoder, &picture);
    if (encoded != NV_ENC_SUCCESS) {
        error = encoded == NV_ENC_ERR_NEED_MORE_INPUT
                    ? "NVENC unexpectedly buffered input with the synchronous "
                      "no-B-frame profile"
                    : "nvEncEncodePicture failed";
        return MKVC_ERROR_CODEC;
    }
    result = emit_packet(state, error);
    if (result == MKVC_OK) {
        ++state.frame_index;
        state.next_pts = std::max(state.next_pts, pts + duration);
    }
    return result;
#endif
}

mkvc_result NvidiaWebmEncoder::flush(std::string& error) {
#if !defined(MKVC_HAS_NVIDIA)
    error = "NVIDIA backend was not built";
    return MKVC_ERROR_NOT_SUPPORTED;
#else
    auto& state = *impl_;
    if (state.closed || state.frame_index == 0) return MKVC_OK;
    NV_ENC_PIC_PARAMS eos{};
    eos.version = NV_ENC_PIC_PARAMS_VER;
    eos.encodePicFlags = NV_ENC_PIC_FLAG_EOS;
    if (state.functions.nvEncEncodePicture(state.encoder, &eos) != NV_ENC_SUCCESS) {
        error = "NVENC flush failed";
        return MKVC_ERROR_CODEC;
    }
    destroy_session(state);
    state.context_anchor.reset();
    state.external_context = nullptr;
    state.frame_index = 0;
    return initialize_session(state, error) ? MKVC_OK : MKVC_ERROR_CODEC;
#endif
}

mkvc_result NvidiaWebmEncoder::close(std::string& error) {
    if (impl_->closed) return MKVC_OK;
    mkvc_result result = MKVC_OK;
#if !defined(MKVC_HAS_NVIDIA)
    (void)error;
#else
    auto& state = *impl_;
    if (state.encoder != nullptr && state.frame_index > 0) {
        NV_ENC_PIC_PARAMS eos{};
        eos.version = NV_ENC_PIC_PARAMS_VER;
        eos.encodePicFlags = NV_ENC_PIC_FLAG_EOS;
        if (state.functions.nvEncEncodePicture(state.encoder, &eos) != NV_ENC_SUCCESS) {
            error = "NVENC close drain failed";
            result = MKVC_ERROR_CODEC;
        }
    }
    destroy_session(state);
    state.context_anchor.reset();
    state.external_context = nullptr;
    if (result == MKVC_OK && state.muxer) result = state.muxer->finalize(error);
#endif
    impl_->closed = true;
    return result;
}

uint32_t NvidiaWebmEncoder::max_pending_observed() const { return 1; }

}  // namespace mkvc
