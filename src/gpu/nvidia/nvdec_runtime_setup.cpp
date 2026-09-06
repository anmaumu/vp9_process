#include "nvdec_runtime_setup.hpp"

namespace mkvc::gpu::nvidia {
namespace {

void discard_runtime(NvdecParserRuntime& runtime, bool context_is_current) noexcept {
    if (!runtime.api) return;
    bool context_active = context_is_current;
    if (!context_active && runtime.context != nullptr) {
        context_active = runtime.api->context_push(runtime.context) == CUDA_SUCCESS;
    }
    if (context_active && runtime.parser != nullptr) {
        (void)runtime.api->parser_destroy(runtime.parser);
        runtime.parser = nullptr;
    }
    if (runtime.context != nullptr) {
        if (context_active) {
            CUcontext popped = nullptr;
            (void)runtime.api->context_pop(&popped);
        }
        (void)runtime.api->context_destroy(runtime.context);
        runtime.context = nullptr;
    }
    runtime.api.reset();
}

}  // namespace

mkvc_result create_nvdec_parser_runtime(mkvc_codec codec, void* user_data,
                                        PFNVIDSEQUENCECALLBACK sequence_callback,
                                        PFNVIDDECODECALLBACK decode_callback,
                                        PFNVIDDISPLAYCALLBACK display_callback,
                                        NvdecParserRuntime& runtime, std::string& error) {
    runtime = {};
    runtime.api = NvdecApi::load(error);
    if (!runtime.api) return MKVC_ERROR_CODEC;

    CUdevice device = 0;
    if (runtime.api->cu_init(0) != CUDA_SUCCESS ||
        runtime.api->device_get(&device, 0) != CUDA_SUCCESS ||
        runtime.api->context_create(&runtime.context, 0, device) != CUDA_SUCCESS) {
        error = "failed to create NVIDIA CUDA context";
        discard_runtime(runtime, runtime.context != nullptr);
        return MKVC_ERROR_CODEC;
    }

    CUVIDPARSERPARAMS params{};
    params.CodecType = codec == MKVC_CODEC_VP9 ? cudaVideoCodec_VP9 : cudaVideoCodec_AV1;
    params.ulMaxNumDecodeSurfaces = 20;
    params.ulClockRate = 1000000000U;
    params.ulMaxDisplayDelay = 2;
    params.pUserData = user_data;
    params.pfnSequenceCallback = sequence_callback;
    params.pfnDecodePicture = decode_callback;
    params.pfnDisplayPicture = display_callback;
    if (runtime.api->parser_create(&runtime.parser, &params) != CUDA_SUCCESS) {
        error = "cuvidCreateVideoParser failed";
        discard_runtime(runtime, true);
        return MKVC_ERROR_CODEC;
    }

    CUcontext popped = nullptr;
    const CUresult pop_result = runtime.api->context_pop(&popped);
    if (pop_result != CUDA_SUCCESS || popped != runtime.context) {
        error = "failed to detach NVIDIA CUDA context";
        discard_runtime(runtime, pop_result != CUDA_SUCCESS);
        return MKVC_ERROR_CODEC;
    }
    return MKVC_OK;
}

}  // namespace mkvc::gpu::nvidia
