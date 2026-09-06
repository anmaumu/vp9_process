#include "nvenc_session.hpp"

#include "nvenc_api.hpp"

namespace mkvc::gpu::nvidia {

mkvc_result initialize_nvenc_session(NvencApi& api, const NvencSessionConfig& config,
                                     CUcontext external_context, NvencSession& session,
                                     std::string& error) {
    if (session.context != nullptr || session.encoder != nullptr || session.input != nullptr ||
        session.output != nullptr) {
        error = "NVENC session resources are already initialized";
        return MKVC_ERROR_INVALID_STATE;
    }
    if (external_context != nullptr) {
        session.context = external_context;
        session.owns_context = false;
    } else {
        CUdevice device = 0;
        if (api.cu_init(0) != CUDA_SUCCESS || api.device_get(&device, 0) != CUDA_SUCCESS ||
            api.context_create(&session.context, 0, device) != CUDA_SUCCESS) {
            error = "failed to create NVIDIA encode CUDA context";
            return MKVC_ERROR_CODEC;
        }
        session.owns_context = true;
    }

    NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS open{};
    open.version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
    open.deviceType = NV_ENC_DEVICE_TYPE_CUDA;
    open.device = session.context;
    open.apiVersion = NVENCAPI_VERSION;
    if (api.functions.nvEncOpenEncodeSessionEx(&open, &session.encoder) != NV_ENC_SUCCESS) {
        error = "nvEncOpenEncodeSessionEx failed";
        return MKVC_ERROR_CODEC;
    }

    NV_ENC_PRESET_CONFIG preset{};
    preset.version = NV_ENC_PRESET_CONFIG_VER;
    preset.presetCfg.version = NV_ENC_CONFIG_VER;
    if (api.functions.nvEncGetEncodePresetConfigEx(
            session.encoder, NV_ENC_CODEC_AV1_GUID, NV_ENC_PRESET_P4_GUID,
            NV_ENC_TUNING_INFO_HIGH_QUALITY, &preset) != NV_ENC_SUCCESS) {
        error = "NVENC AV1 P4 preset query failed";
        return MKVC_ERROR_CODEC;
    }
    NV_ENC_CONFIG encode_config = preset.presetCfg;
    encode_config.version = NV_ENC_CONFIG_VER;
    encode_config.profileGUID = NV_ENC_AV1_PROFILE_MAIN_GUID;
    encode_config.gopLength = config.keyframe_interval;
    encode_config.frameIntervalP = 1;
    encode_config.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CONSTQP;
    encode_config.rcParams.constQP.qpInterP = config.quality;
    encode_config.rcParams.constQP.qpInterB = config.quality;
    encode_config.rcParams.constQP.qpIntra = config.quality;

    NV_ENC_INITIALIZE_PARAMS init{};
    init.version = NV_ENC_INITIALIZE_PARAMS_VER;
    init.encodeGUID = NV_ENC_CODEC_AV1_GUID;
    init.presetGUID = NV_ENC_PRESET_P4_GUID;
    init.encodeWidth = config.width;
    init.encodeHeight = config.height;
    init.darWidth = config.width;
    init.darHeight = config.height;
    init.frameRateNum = config.fps_num;
    init.frameRateDen = config.fps_den;
    init.enableEncodeAsync = 0;
    init.enablePTD = 1;
    init.maxEncodeWidth = config.width;
    init.maxEncodeHeight = config.height;
    init.tuningInfo = NV_ENC_TUNING_INFO_HIGH_QUALITY;
    init.encodeConfig = &encode_config;
    if (api.functions.nvEncInitializeEncoder(session.encoder, &init) != NV_ENC_SUCCESS) {
        error = "nvEncInitializeEncoder AV1 failed";
        return MKVC_ERROR_CODEC;
    }

    NV_ENC_CREATE_INPUT_BUFFER input{};
    input.version = NV_ENC_CREATE_INPUT_BUFFER_VER;
    input.width = config.width;
    input.height = config.height;
    input.bufferFmt = NV_ENC_BUFFER_FORMAT_NV12;
    if (api.functions.nvEncCreateInputBuffer(session.encoder, &input) != NV_ENC_SUCCESS) {
        error = "nvEncCreateInputBuffer failed";
        return MKVC_ERROR_CODEC;
    }
    session.input = input.inputBuffer;

    NV_ENC_CREATE_BITSTREAM_BUFFER output{};
    output.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;
    if (api.functions.nvEncCreateBitstreamBuffer(session.encoder, &output) != NV_ENC_SUCCESS) {
        error = "nvEncCreateBitstreamBuffer failed";
        return MKVC_ERROR_CODEC;
    }
    session.output = output.bitstreamBuffer;
    return MKVC_OK;
}

void destroy_nvenc_session(NvencApi& api, NvencSession& session) noexcept {
    if (session.encoder != nullptr) {
        if (session.input != nullptr)
            (void)api.functions.nvEncDestroyInputBuffer(session.encoder, session.input);
        if (session.output != nullptr)
            (void)api.functions.nvEncDestroyBitstreamBuffer(session.encoder, session.output);
        (void)api.functions.nvEncDestroyEncoder(session.encoder);
    }
    session.input = nullptr;
    session.output = nullptr;
    session.encoder = nullptr;
    if (session.context != nullptr && session.owns_context)
        (void)api.context_destroy(session.context);
    session.context = nullptr;
    session.owns_context = false;
}

}  // namespace mkvc::gpu::nvidia
