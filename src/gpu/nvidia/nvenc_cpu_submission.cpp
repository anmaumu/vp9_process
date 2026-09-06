#include "nvenc_cpu_submission.hpp"

#include <ffnvcodec/nvEncodeAPI.h>

#include <cstddef>
#include <cstring>

#include "nvenc_api.hpp"
#include "nvenc_session.hpp"

namespace mkvc::gpu::nvidia {
namespace {

void copy_plane(uint8_t* destination, uint32_t destination_stride, const uint8_t* source,
                uint32_t source_stride, uint32_t width, uint32_t height) {
    for (uint32_t row = 0; row < height; ++row)
        std::memcpy(destination + static_cast<size_t>(row) * destination_stride,
                    source + static_cast<size_t>(row) * source_stride, width);
}

}  // namespace

mkvc_result submit_nvenc_cpu_frame(NvencApi& api, const NvencSession& session, const uint8_t* nv12,
                                   uint32_t width, uint32_t height, uint64_t frame_index,
                                   int64_t pts_ns, int64_t duration_ns, bool force_keyframe,
                                   std::string& error) {
    if (nv12 == nullptr) {
        error = "NVENC CPU input is null";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    NV_ENC_LOCK_INPUT_BUFFER lock{};
    lock.version = NV_ENC_LOCK_INPUT_BUFFER_VER;
    lock.inputBuffer = session.input;
    if (api.functions.nvEncLockInputBuffer(session.encoder, &lock) != NV_ENC_SUCCESS) {
        error = "nvEncLockInputBuffer failed";
        return MKVC_ERROR_CODEC;
    }
    if (lock.bufferDataPtr == nullptr || lock.pitch < width) {
        (void)api.functions.nvEncUnlockInputBuffer(session.encoder, session.input);
        error = "NVENC returned an invalid CPU input buffer layout";
        return MKVC_ERROR_CODEC;
    }

    const uint8_t* source_uv = nv12 + static_cast<size_t>(width) * height;
    auto* destination = static_cast<uint8_t*>(lock.bufferDataPtr);
    copy_plane(destination, lock.pitch, nv12, width, width, height);
    copy_plane(destination + static_cast<size_t>(lock.pitch) * height, lock.pitch, source_uv, width,
               width, height / 2);
    (void)api.functions.nvEncUnlockInputBuffer(session.encoder, session.input);

    NV_ENC_PIC_PARAMS picture{};
    picture.version = NV_ENC_PIC_PARAMS_VER;
    picture.inputWidth = width;
    picture.inputHeight = height;
    picture.inputPitch = lock.pitch;
    picture.inputBuffer = session.input;
    picture.outputBitstream = session.output;
    picture.bufferFmt = NV_ENC_BUFFER_FORMAT_NV12;
    picture.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
    picture.frameIdx = static_cast<uint32_t>(frame_index);
    picture.inputTimeStamp = static_cast<uint64_t>(pts_ns);
    picture.inputDuration = static_cast<uint64_t>(duration_ns);
    if (force_keyframe) picture.encodePicFlags = NV_ENC_PIC_FLAG_FORCEIDR;
    const NVENCSTATUS encoded = api.functions.nvEncEncodePicture(session.encoder, &picture);
    if (encoded != NV_ENC_SUCCESS) {
        error = encoded == NV_ENC_ERR_NEED_MORE_INPUT
                    ? "NVENC unexpectedly buffered input with the synchronous no-B-frame profile"
                    : "nvEncEncodePicture failed";
        return MKVC_ERROR_CODEC;
    }
    return MKVC_OK;
}

}  // namespace mkvc::gpu::nvidia
