#include "intel_vpl_probe.hpp"

#include <cstdlib>
#include <iostream>

int main() {
    const mkvc::IntelVplProbeResult result = mkvc::probe_intel_vpl();
    if (!result.runtime_available) {
        std::cout << "oneVPL hardware unavailable: "
                  << result.unavailable_reason << '\n';
        return std::getenv("MKVC_REQUIRE_INTEL_GPU") != nullptr ? 1 : 77;
    }
    std::cout << "oneVPL API " << result.api_major << '.' << result.api_minor
              << " VP9(dec=" << result.vp9_decode
              << ",enc=" << result.vp9_encode << ") AV1(dec="
              << result.av1_decode << ",enc=" << result.av1_encode << ")\n";
    if (result.api_major < 2 ||
        (result.api_major == 2 && result.api_minor < 10)) {
        return 1;
    }
    return (result.vp9_decode && result.vp9_encode &&
            result.av1_decode && result.av1_encode) ? 0 : 1;
}
