#pragma once

#include <ffnvcodec/dynlink_cuda.h>
#include <ffnvcodec/dynlink_cuviddec.h>
#include <ffnvcodec/dynlink_nvcuvid.h>

#include <memory>
#include <string>

#include "mkvcodec/mkvc.h"
#include "nvdec_api.hpp"

namespace mkvc::gpu::nvidia {

/** Resources produced by the detached NVDEC parser initialization sequence. */
struct NvdecParserRuntime {
    std::unique_ptr<NvdecApi> api;
    CUcontext context = nullptr;
    CUvideoparser parser = nullptr;
};

/**
 * @brief Load NVDEC, create its CUDA context/parser and detach the context.
 *
 * @param codec VP9 or AV1 parser codec.
 * @param user_data Opaque state forwarded to every parser callback.
 * @param sequence_callback Sequence callback.
 * @param decode_callback Decode-picture callback.
 * @param display_callback Display-order callback.
 * @param runtime Receives fully initialized resources only on success.
 * @param error Receives initialization or context-detach diagnostics.
 * @return MKVC_OK, or MKVC_ERROR_CODEC when initialization fails.
 *
 * Partially created parser/context resources are released before an error is
 * returned. The successful context is not current on the calling thread.
 */
mkvc_result create_nvdec_parser_runtime(mkvc_codec codec, void* user_data,
                                        PFNVIDSEQUENCECALLBACK sequence_callback,
                                        PFNVIDDECODECALLBACK decode_callback,
                                        PFNVIDDISPLAYCALLBACK display_callback,
                                        NvdecParserRuntime& runtime, std::string& error);

}  // namespace mkvc::gpu::nvidia
