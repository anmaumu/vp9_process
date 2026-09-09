/**
 * @file gpu_frame_handle.hpp
 * @brief Private layout of the opaque C ABI GPU-frame handle.
 */
#pragma once

#include "gpu_frame.hpp"

#include <atomic>
#include <cstdint>
#include <memory>

/** Reference-counted ABI handle; its layout never appears in the public header. */
struct mkvc_gpu_frame {
    std::atomic<uint32_t> references{1};
    std::shared_ptr<mkvc::gpu::GpuFrameCore> core;
    uint64_t generation = 0;
};
