#include "nvidia_probe.hpp"

#include <cstdlib>
#include <iostream>

int main() {
    const mkvc::NvidiaProbeResult result = mkvc::probe_nvidia();
    if (!result.runtime_available) {
        std::cout << "NVIDIA hardware unavailable: "
                  << result.unavailable_reason << '\n';
        return std::getenv("MKVC_REQUIRE_NVIDIA_GPU") != nullptr ? 1 : 77;
    }
    std::cout << result.device_name << " compute " << result.compute_major
              << '.' << result.compute_minor << " CUDA driver "
              << result.cuda_driver_version << " NVENC API 0x" << std::hex
              << result.nvenc_max_api_version << std::dec << " VP9(dec="
              << result.vp9_decode << ") AV1(dec=" << result.av1_decode
              << ",enc=" << result.av1_encode << ")\n";
    return 0;
}
