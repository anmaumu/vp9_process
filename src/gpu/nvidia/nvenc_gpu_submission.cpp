#include "nvenc_gpu_submission.hpp"

#include <cstdint>

#include "nvenc_api.hpp"
#include "nvenc_session.hpp"

namespace mkvc::gpu::nvidia {

mkvc_result submit_nvenc_cuda_frame(NvencApi& api, const NvencSession& session, bool cuda_array,
                                    uint64_t resource_handle, uint32_t width, uint32_t height,
                                    uint32_t pitch, uint64_t frame_index, int64_t pts_ns,
                                    int64_t duration_ns, bool force_keyframe, std::string& error) {
    NV_ENC_REGISTER_RESOURCE registration{};
    registration.version = NV_ENC_REGISTER_RESOURCE_VER;
    registration.resourceType = cuda_array ? NV_ENC_INPUT_RESOURCE_TYPE_CUDAARRAY
                                           : NV_ENC_INPUT_RESOURCE_TYPE_CUDADEVICEPTR;
    registration.resourceToRegister =
        reinterpret_cast<void*>(static_cast<uintptr_t>(resource_handle));
    registration.width = width;
    registration.height = height;
    registration.pitch = pitch;
    registration.bufferFormat = NV_ENC_BUFFER_FORMAT_NV12;
    registration.bufferUsage = NV_ENC_INPUT_IMAGE;
    if (api.functions.nvEncRegisterResource(session.encoder, &registration) != NV_ENC_SUCCESS) {
        error = "nvEncRegisterResource rejected the CUDA input resource";
        return MKVC_ERROR_CODEC;
    }

    NV_ENC_MAP_INPUT_RESOURCE mapping{};
    mapping.version = NV_ENC_MAP_INPUT_RESOURCE_VER;
    mapping.registeredResource = registration.registeredResource;
    if (api.functions.nvEncMapInputResource(session.encoder, &mapping) != NV_ENC_SUCCESS) {
        (void)api.functions.nvEncUnregisterResource(session.encoder,
                                                    registration.registeredResource);
        error = "nvEncMapInputResource failed";
        return MKVC_ERROR_CODEC;
    }

    NV_ENC_PIC_PARAMS picture{};
    picture.version = NV_ENC_PIC_PARAMS_VER;
    picture.inputWidth = width;
    picture.inputHeight = height;
    picture.inputPitch = pitch;
    picture.inputBuffer = mapping.mappedResource;
    picture.outputBitstream = session.output;
    picture.bufferFmt = mapping.mappedBufferFmt;
    picture.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
    picture.frameIdx = static_cast<uint32_t>(frame_index);
    picture.inputTimeStamp = static_cast<uint64_t>(pts_ns);
    picture.inputDuration = static_cast<uint64_t>(duration_ns);
    if (force_keyframe) picture.encodePicFlags = NV_ENC_PIC_FLAG_FORCEIDR;
    const NVENCSTATUS encoded = api.functions.nvEncEncodePicture(session.encoder, &picture);

    (void)api.functions.nvEncUnmapInputResource(session.encoder, mapping.mappedResource);
    (void)api.functions.nvEncUnregisterResource(session.encoder, registration.registeredResource);
    if (encoded != NV_ENC_SUCCESS) {
        error = "nvEncEncodePicture failed for GPU input";
        return MKVC_ERROR_CODEC;
    }
    return MKVC_OK;
}

}  // namespace mkvc::gpu::nvidia
