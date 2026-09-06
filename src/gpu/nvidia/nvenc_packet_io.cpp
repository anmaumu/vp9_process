#include "nvenc_packet_io.hpp"

#include <ffnvcodec/nvEncodeAPI.h>

#include "nvenc_api.hpp"
#include "nvenc_session.hpp"
#include "webm_muxer.hpp"

namespace mkvc::gpu::nvidia {

mkvc_result mux_nvenc_packet(NvencApi& api, const NvencSession& session, WebmMuxer& muxer,
                             uint64_t default_duration_ns, std::string& error) {
    NV_ENC_LOCK_BITSTREAM lock{};
    lock.version = NV_ENC_LOCK_BITSTREAM_VER;
    lock.outputBitstream = session.output;
    if (api.functions.nvEncLockBitstream(session.encoder, &lock) != NV_ENC_SUCCESS) {
        error = "nvEncLockBitstream failed";
        return MKVC_ERROR_CODEC;
    }

    mkvc_result result = MKVC_ERROR_CODEC;
    if (lock.bitstreamBufferPtr == nullptr || lock.bitstreamSizeInBytes == 0) {
        error = "NVENC returned an empty bitstream buffer";
    } else {
        result = muxer.add_frame(
            static_cast<const uint8_t*>(lock.bitstreamBufferPtr), lock.bitstreamSizeInBytes,
            lock.outputTimeStamp,
            lock.outputDuration != 0 ? lock.outputDuration : default_duration_ns,
            lock.pictureType == NV_ENC_PIC_TYPE_IDR || lock.pictureType == NV_ENC_PIC_TYPE_I,
            error);
    }
    (void)api.functions.nvEncUnlockBitstream(session.encoder, session.output);
    return result;
}

mkvc_result drain_nvenc_session(NvencApi& api, const NvencSession& session, std::string& error) {
    NV_ENC_PIC_PARAMS eos{};
    eos.version = NV_ENC_PIC_PARAMS_VER;
    eos.encodePicFlags = NV_ENC_PIC_FLAG_EOS;
    if (api.functions.nvEncEncodePicture(session.encoder, &eos) != NV_ENC_SUCCESS) {
        error = "NVENC session drain failed";
        return MKVC_ERROR_CODEC;
    }
    return MKVC_OK;
}

}  // namespace mkvc::gpu::nvidia
