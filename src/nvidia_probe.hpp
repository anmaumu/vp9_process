#pragma once

#include <cstdint>
#include <string>

namespace mkvc {

/** Runtime CUDA, NVDEC, and NVENC capability query result for device zero. */
struct NvidiaProbeResult {
    bool runtime_available = false;
    bool vp9_decode = false;
    bool av1_decode = false;
    bool av1_encode = false;
    int compute_major = 0;
    int compute_minor = 0;
    int cuda_driver_version = 0;
    uint32_t nvenc_max_api_version = 0;
    std::string device_name;
    std::string unavailable_reason;
};

/** Probe vendor driver APIs without linking or advertising public backends. */
NvidiaProbeResult probe_nvidia();

}  // namespace mkvc
