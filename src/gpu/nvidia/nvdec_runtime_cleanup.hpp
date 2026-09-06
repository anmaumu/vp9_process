#pragma once

#include <ffnvcodec/dynlink_cuda.h>
#include <ffnvcodec/dynlink_cuviddec.h>
#include <ffnvcodec/dynlink_nvcuvid.h>

#include <string>

namespace mkvc::gpu::nvidia {

class NvdecApi;

/** Destroy a parser while its CUDA context is temporarily active. */
bool destroy_nvdec_parser(NvdecApi& api, CUcontext context, CUvideoparser& parser,
                          std::string& error) noexcept;

/** Unmap a leased NVDEC frame while its CUDA context is temporarily active. */
bool release_nvdec_mapping(NvdecApi& api, CUcontext context, CUvideodecoder decoder,
                           unsigned long long device_pointer, std::string& error) noexcept;

/** Destroy the decoder, detach its context, and finally destroy the context. */
bool destroy_nvdec_decoder_context(NvdecApi& api, CUcontext& context, CUvideodecoder& decoder,
                                   std::string& error) noexcept;

}  // namespace mkvc::gpu::nvidia
