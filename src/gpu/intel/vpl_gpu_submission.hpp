#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "intel_vpl_encoder.hpp"

namespace mkvc::gpu {
class GpuFrameCore;
namespace intel {

/**
 * Submit one Intel NV12 surface while preserving producer/consumer lifetime.
 *
 * The adapter assigns the oneVPL timestamp, imports external VA/D3D11
 * resources when needed, binds a completion lease, and advances the ordered
 * queue enough to avoid starving a smaller upstream decoder pool.
 */
mkvc_result submit_gpu_surface(IntelVplEncoder::Impl& impl,
                               const std::shared_ptr<GpuFrameCore>& frame, int64_t requested_pts,
                               std::vector<IntelEncodedPacket>& packets, std::string& error);

}  // namespace intel
}  // namespace mkvc::gpu
