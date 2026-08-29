#pragma once

#include <cstdint>
#include <string>

namespace mkvc {

/** Runtime oneVPL hardware-session and codec query result. */
struct IntelVplProbeResult {
    bool runtime_available = false;
    bool vp9_decode = false;
    bool vp9_encode = false;
    bool av1_decode = false;
    bool av1_encode = false;
    uint16_t api_major = 0;
    uint16_t api_minor = 0;
    std::string unavailable_reason;
};

/** Probe oneVPL without advertising not-yet-wired public codec backends. */
IntelVplProbeResult probe_intel_vpl();

}  // namespace mkvc
