#pragma once

/**
 * @file copy_edge_metrics.hpp
 * @brief Lock-free counters for explicitly observed memory-transfer edges.
 */

#include <atomic>
#include <cstddef>

#include "mkvcodec/mkvc.h"

namespace mkvc {

/** Copy or sharing operation observed at a library-controlled boundary. */
enum class CopyEdge : size_t {
    kSharedSurface,
    kZeroCopy,
    kGpuCopy,
    kCpuUpload,
    kCpuReadback,
    kCpuNormalization,
    kPixelConversion,
    kCount
};

/** Thread-safe cumulative copy-edge observations. */
class CopyEdgeMetricsAccumulator {
   public:
    /** Record one operation only after that operation has been selected or completed. */
    void add(CopyEdge edge) noexcept {
        counters_[static_cast<size_t>(edge)].fetch_add(1, std::memory_order_relaxed);
    }

    /** Return a versioned public snapshot without inferring driver behavior. */
    mkvc_copy_edge_metrics snapshot() const noexcept {
        mkvc_copy_edge_metrics result{};
        result.struct_size = sizeof(result);
        result.struct_version = 1;
        result.shared_surface_frames = load(CopyEdge::kSharedSurface);
        result.zero_copy_frames = load(CopyEdge::kZeroCopy);
        result.gpu_copy_frames = load(CopyEdge::kGpuCopy);
        result.cpu_upload_frames = load(CopyEdge::kCpuUpload);
        result.cpu_readback_frames = load(CopyEdge::kCpuReadback);
        result.cpu_normalization_frames = load(CopyEdge::kCpuNormalization);
        result.pixel_conversion_frames = load(CopyEdge::kPixelConversion);
        result.driver_internal_observed = 0;
        return result;
    }

   private:
    std::atomic<uint64_t> counters_[static_cast<size_t>(CopyEdge::kCount)]{};

    uint64_t load(CopyEdge edge) const noexcept {
        return counters_[static_cast<size_t>(edge)].load(std::memory_order_relaxed);
    }
};

}  // namespace mkvc
