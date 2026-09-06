#pragma once

#include <ffnvcodec/dynlink_cuda.h>
#include <ffnvcodec/nvEncodeAPI.h>

#include <memory>
#include <string>

namespace mkvc::gpu::nvidia {

class DynamicLibrary;

/**
 * @brief Required CUDA driver and NVENC entry points for the AV1 adapter.
 *
 * `load` fails unless both driver libraries, all CUDA context functions and the
 * required NVENC function table entries are available. The object owns the
 * module handles for the full lifetime of every resolved function pointer.
 */
class NvencApi final {
   public:
    /** Load the platform driver libraries and validate required entry points. */
    static std::unique_ptr<NvencApi> load(std::string& error);
    ~NvencApi();
    NvencApi(const NvencApi&) = delete;
    NvencApi& operator=(const NvencApi&) = delete;

    tcuInit* cu_init = nullptr;
    tcuDeviceGet* device_get = nullptr;
    tcuCtxCreate_v2* context_create = nullptr;
    tcuCtxDestroy_v2* context_destroy = nullptr;
    NV_ENCODE_API_FUNCTION_LIST functions{};

   private:
    NvencApi();
    std::unique_ptr<DynamicLibrary> cuda_;
    std::unique_ptr<DynamicLibrary> nvenc_;
};

}  // namespace mkvc::gpu::nvidia
