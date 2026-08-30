#include "nvidia_probe.hpp"

#ifdef MKVC_HAS_NVIDIA

#include <ffnvcodec/dynlink_cuda.h>
#include <ffnvcodec/dynlink_cuviddec.h>
#include <ffnvcodec/nvEncodeAPI.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace mkvc {
namespace {

class DynamicLibrary {
public:
    explicit DynamicLibrary(const char* name) {
#ifdef _WIN32
        handle_ = LoadLibraryA(name);
#else
        handle_ = dlopen(name, RTLD_LAZY | RTLD_LOCAL);
#endif
    }
    ~DynamicLibrary() {
#ifdef _WIN32
        if (handle_ != nullptr) FreeLibrary(handle_);
#else
        if (handle_ != nullptr) dlclose(handle_);
#endif
    }
    DynamicLibrary(const DynamicLibrary&) = delete;
    DynamicLibrary& operator=(const DynamicLibrary&) = delete;
    explicit operator bool() const { return handle_ != nullptr; }

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

private:
#ifdef _WIN32
    HMODULE handle_ = nullptr;
#else
    void* handle_ = nullptr;
#endif
};

bool same_guid(const GUID& lhs, const GUID& rhs) {
    return std::memcmp(&lhs, &rhs, sizeof(GUID)) == 0;
}

}  // namespace

NvidiaProbeResult probe_nvidia() {
    NvidiaProbeResult result;
#ifdef _WIN32
    DynamicLibrary cuda("nvcuda.dll");
    DynamicLibrary cuvid("nvcuvid.dll");
    DynamicLibrary nvenc("nvEncodeAPI64.dll");
#else
    DynamicLibrary cuda("libcuda.so.1");
    DynamicLibrary cuvid("libnvcuvid.so.1");
    DynamicLibrary nvenc("libnvidia-encode.so.1");
#endif
    if (!cuda) {
        result.unavailable_reason = "CUDA driver library not found";
        return result;
    }

    const auto cu_init = cuda.symbol<tcuInit*>("cuInit");
    const auto device_count = cuda.symbol<tcuDeviceGetCount*>("cuDeviceGetCount");
    const auto device_get = cuda.symbol<tcuDeviceGet*>("cuDeviceGet");
    const auto device_name = cuda.symbol<tcuDeviceGetName*>("cuDeviceGetName");
    const auto compute_capability =
        cuda.symbol<tcuDeviceComputeCapability*>("cuDeviceComputeCapability");
    const auto driver_version =
        cuda.symbol<tcuDriverGetVersion*>("cuDriverGetVersion");
    const auto context_create = cuda.symbol<tcuCtxCreate_v2*>("cuCtxCreate_v2");
    const auto context_destroy =
        cuda.symbol<tcuCtxDestroy_v2*>("cuCtxDestroy_v2");
    if (cu_init == nullptr || device_count == nullptr || device_get == nullptr ||
        device_name == nullptr || compute_capability == nullptr ||
        driver_version == nullptr || context_create == nullptr ||
        context_destroy == nullptr) {
        result.unavailable_reason = "CUDA driver is missing required symbols";
        return result;
    }

    int count = 0;
    CUdevice device = 0;
    if (cu_init(0) != CUDA_SUCCESS || device_count(&count) != CUDA_SUCCESS ||
        count < 1 || device_get(&device, 0) != CUDA_SUCCESS) {
        result.unavailable_reason = "no usable CUDA device";
        return result;
    }
    result.runtime_available = true;
    char name[256] = {};
    if (device_name(name, static_cast<int>(sizeof(name)), device) == CUDA_SUCCESS) {
        result.device_name = name;
    }
    (void)compute_capability(&result.compute_major, &result.compute_minor, device);
    (void)driver_version(&result.cuda_driver_version);

    CUcontext context = nullptr;
    if (context_create(&context, 0, device) != CUDA_SUCCESS) {
        result.unavailable_reason = "CUDA context creation failed";
        result.runtime_available = false;
        return result;
    }

    if (cuvid) {
        const auto decoder_caps =
            cuvid.symbol<tcuvidGetDecoderCaps*>("cuvidGetDecoderCaps");
        if (decoder_caps != nullptr) {
            const auto supported = [decoder_caps](cudaVideoCodec codec) {
                CUVIDDECODECAPS caps{};
                caps.eCodecType = codec;
                caps.eChromaFormat = cudaVideoChromaFormat_420;
                caps.nBitDepthMinus8 = 0;
                return decoder_caps(&caps) == CUDA_SUCCESS &&
                       caps.bIsSupported != 0;
            };
            result.vp9_decode = supported(cudaVideoCodec_VP9);
            result.av1_decode = supported(cudaVideoCodec_AV1);
        }
    }

    if (nvenc) {
        using GetMaxVersion = NVENCSTATUS(NVENCAPI*)(uint32_t*);
        using CreateInstance =
            NVENCSTATUS(NVENCAPI*)(NV_ENCODE_API_FUNCTION_LIST*);
        const auto get_max =
            nvenc.symbol<GetMaxVersion>("NvEncodeAPIGetMaxSupportedVersion");
        const auto create_instance =
            nvenc.symbol<CreateInstance>("NvEncodeAPICreateInstance");
        if (get_max != nullptr && create_instance != nullptr &&
            get_max(&result.nvenc_max_api_version) == NV_ENC_SUCCESS &&
            result.nvenc_max_api_version >= NVENCAPI_VERSION) {
            NV_ENCODE_API_FUNCTION_LIST functions{};
            functions.version = NV_ENCODE_API_FUNCTION_LIST_VER;
            if (create_instance(&functions) == NV_ENC_SUCCESS &&
                functions.nvEncOpenEncodeSessionEx != nullptr &&
                functions.nvEncGetEncodeGUIDCount != nullptr &&
                functions.nvEncGetEncodeGUIDs != nullptr &&
                functions.nvEncDestroyEncoder != nullptr) {
                NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS open{};
                open.version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
                open.deviceType = NV_ENC_DEVICE_TYPE_CUDA;
                open.device = context;
                open.apiVersion = NVENCAPI_VERSION;
                void* encoder = nullptr;
                if (functions.nvEncOpenEncodeSessionEx(&open, &encoder) ==
                    NV_ENC_SUCCESS) {
                    uint32_t count_guids = 0;
                    if (functions.nvEncGetEncodeGUIDCount(
                            encoder, &count_guids) == NV_ENC_SUCCESS) {
                        std::vector<GUID> guids(count_guids);
                        uint32_t actual = 0;
                        if (functions.nvEncGetEncodeGUIDs(
                                encoder, guids.data(), count_guids, &actual) ==
                            NV_ENC_SUCCESS) {
                            actual = (std::min)(actual, count_guids);
                            result.av1_encode = std::any_of(
                                guids.begin(), guids.begin() + actual,
                                [](const GUID& guid) {
                                    return same_guid(guid, NV_ENC_CODEC_AV1_GUID);
                                });
                        }
                    }
                    (void)functions.nvEncDestroyEncoder(encoder);
                }
            }
        }
    }
    (void)context_destroy(context);
    return result;
}

}  // namespace mkvc

#else

namespace mkvc {
NvidiaProbeResult probe_nvidia() {
    NvidiaProbeResult result;
    result.unavailable_reason = "NVIDIA support was disabled at build time";
    return result;
}
}  // namespace mkvc

#endif
