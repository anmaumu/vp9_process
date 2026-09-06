#include "nvdec_api.hpp"

#include "dynamic_library.hpp"

namespace mkvc::gpu::nvidia {

NvdecApi::NvdecApi() = default;
NvdecApi::~NvdecApi() = default;

std::unique_ptr<NvdecApi> NvdecApi::load(std::string& error) {
    auto api = std::unique_ptr<NvdecApi>(new NvdecApi());
#if defined(_WIN32)
    api->cuda_ = std::make_unique<DynamicLibrary>("nvcuda.dll");
    api->cuvid_ = std::make_unique<DynamicLibrary>("nvcuvid.dll");
#else
    api->cuda_ = std::make_unique<DynamicLibrary>("libcuda.so.1");
    api->cuvid_ = std::make_unique<DynamicLibrary>("libnvcuvid.so.1");
#endif
    if (!*api->cuda_ || !*api->cuvid_) {
        error = "NVIDIA CUDA/NVCUVID driver libraries not found";
        return nullptr;
    }
#define MKVC_LOAD(member, library, type, symbol_name) \
    api->member = api->library##_->symbol<type*>(symbol_name)
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
    if (api->cu_init == nullptr || api->device_get == nullptr || api->context_create == nullptr ||
        api->context_destroy == nullptr || api->context_push == nullptr ||
        api->context_pop == nullptr || api->memcpy_2d == nullptr || api->parser_create == nullptr ||
        api->parser_parse == nullptr || api->parser_destroy == nullptr ||
        api->decoder_create == nullptr || api->decoder_destroy == nullptr ||
        api->decode_picture == nullptr || api->map_frame == nullptr ||
        api->unmap_frame == nullptr || api->decoder_caps == nullptr) {
        error = "NVIDIA driver is missing required NVDEC symbols";
        return nullptr;
    }
    return api;
}

}  // namespace mkvc::gpu::nvidia
