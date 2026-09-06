#pragma once

#include <ffnvcodec/dynlink_cuda.h>
#include <ffnvcodec/dynlink_cuviddec.h>
#include <ffnvcodec/dynlink_nvcuvid.h>

#include <memory>
#include <string>

namespace mkvc::gpu::nvidia {

class DynamicLibrary;

/**
 * @brief Required CUDA driver and NVCUVID entry points for the NVDEC adapter.
 *
 * `load` fails unless every required symbol is present. The table owns both
 * driver module handles so resolved pointers remain valid for its full lifetime.
 */
class NvdecApi {
   public:
    /** Load the platform driver libraries and validate every required symbol. */
    static std::unique_ptr<NvdecApi> load(std::string& error);
    ~NvdecApi();
    NvdecApi(const NvdecApi&) = delete;
    NvdecApi& operator=(const NvdecApi&) = delete;

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

   private:
    NvdecApi();
    std::unique_ptr<DynamicLibrary> cuda_;
    std::unique_ptr<DynamicLibrary> cuvid_;
};

}  // namespace mkvc::gpu::nvidia
