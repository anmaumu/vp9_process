#include "nvenc_api.hpp"

#include "dynamic_library.hpp"

namespace mkvc::gpu::nvidia {
namespace {

using CreateInstance = NVENCSTATUS(NVENCAPI*)(NV_ENCODE_API_FUNCTION_LIST*);

}  // namespace

NvencApi::NvencApi() = default;
NvencApi::~NvencApi() = default;

std::unique_ptr<NvencApi> NvencApi::load(std::string& error) {
    auto api = std::unique_ptr<NvencApi>(new NvencApi());
#if defined(_WIN32)
    api->cuda_ = std::make_unique<DynamicLibrary>("nvcuda.dll");
    api->nvenc_ = std::make_unique<DynamicLibrary>("nvEncodeAPI64.dll");
#else
    api->cuda_ = std::make_unique<DynamicLibrary>("libcuda.so.1");
    api->nvenc_ = std::make_unique<DynamicLibrary>("libnvidia-encode.so.1");
#endif
    if (!*api->cuda_ || !*api->nvenc_) {
        error = "NVIDIA encode driver libraries not found";
        return nullptr;
    }

    api->cu_init = api->cuda_->symbol<tcuInit*>("cuInit");
    api->device_get = api->cuda_->symbol<tcuDeviceGet*>("cuDeviceGet");
    api->context_create = api->cuda_->symbol<tcuCtxCreate_v2*>("cuCtxCreate_v2");
    api->context_destroy = api->cuda_->symbol<tcuCtxDestroy_v2*>("cuCtxDestroy_v2");
    const auto create_instance = api->nvenc_->symbol<CreateInstance>("NvEncodeAPICreateInstance");
    if (api->cu_init == nullptr || api->device_get == nullptr || api->context_create == nullptr ||
        api->context_destroy == nullptr || create_instance == nullptr) {
        error = "NVIDIA encode driver is missing required symbols";
        return nullptr;
    }

    api->functions.version = NV_ENCODE_API_FUNCTION_LIST_VER;
    if (create_instance(&api->functions) != NV_ENC_SUCCESS ||
        api->functions.nvEncGetEncodePresetConfigEx == nullptr) {
        error = "NvEncodeAPICreateInstance failed";
        return nullptr;
    }
    return api;
}

}  // namespace mkvc::gpu::nvidia
