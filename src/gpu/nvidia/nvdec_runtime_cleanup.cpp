#include "nvdec_runtime_cleanup.hpp"

#include "cuda_context_guard.hpp"
#include "nvdec_api.hpp"

namespace mkvc::gpu::nvidia {

bool destroy_nvdec_parser(NvdecApi& api, CUcontext context, CUvideoparser& parser,
                          std::string& error) noexcept {
    if (parser == nullptr) return true;
    CudaContextGuard context_guard(context, api.context_push, api.context_pop);
    if (!context_guard) {
        error = "failed to activate CUDA context during parser destruction";
        return false;
    }
    const CUresult destroyed = api.parser_destroy(parser);
    parser = nullptr;
    const bool released = context_guard.release();
    if (destroyed != CUDA_SUCCESS) {
        error = "cuvidDestroyVideoParser failed";
        return false;
    }
    if (!released) {
        error = "failed to release CUDA context after parser destruction";
        return false;
    }
    return true;
}

bool release_nvdec_mapping(NvdecApi& api, CUcontext context, CUvideodecoder decoder,
                           unsigned long long device_pointer, std::string& error) noexcept {
    CudaContextGuard context_guard(context, api.context_push, api.context_pop);
    if (!context_guard) {
        error = "failed to activate CUDA context during mapped-frame release";
        return false;
    }
    const CUresult unmapped = api.unmap_frame(decoder, device_pointer);
    const bool released = context_guard.release();
    if (unmapped != CUDA_SUCCESS) {
        error = "cuvidUnmapVideoFrame failed";
        return false;
    }
    if (!released) {
        error = "failed to release CUDA context after mapped-frame release";
        return false;
    }
    return true;
}

bool destroy_nvdec_decoder_context(NvdecApi& api, CUcontext& context, CUvideodecoder& decoder,
                                   std::string& error) noexcept {
    if (context == nullptr) return true;
    CudaContextGuard context_guard(context, api.context_push, api.context_pop);
    if (!context_guard) {
        error = "failed to activate CUDA context during decoder destruction";
        return false;
    }
    CUresult decoder_result = CUDA_SUCCESS;
    if (decoder != nullptr) decoder_result = api.decoder_destroy(decoder);
    decoder = nullptr;
    if (!context_guard.release()) {
        error = "failed to release CUDA context during decoder destruction";
        return false;
    }
    const CUresult context_result = api.context_destroy(context);
    context = nullptr;
    if (decoder_result != CUDA_SUCCESS) {
        error = "cuvidDestroyDecoder failed";
        return false;
    }
    if (context_result != CUDA_SUCCESS) {
        error = "cuCtxDestroy failed";
        return false;
    }
    return true;
}

}  // namespace mkvc::gpu::nvidia
