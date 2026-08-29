#pragma once

#include "gpu_frame.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace mkvc::gpu {

/** Fixed-capacity generation-safe pool for backend GPU resources. */
class GpuFramePool : public std::enable_shared_from_this<GpuFramePool> {
 public:
    using ResourceRecycle = std::function<void()>;
    struct Acquisition {
        std::shared_ptr<GpuFrameCore> core;
        size_t slot = 0;
        uint64_t generation = 0;
    };

    explicit GpuFramePool(size_t capacity);
    mkvc_result acquire(mkvc_gpu_frame_desc desc,
                        std::shared_ptr<Completion> producer,
                        std::optional<mkvc_gpu_native_handle_desc> native,
                        ResourceRecycle resource_recycle,
                        Acquisition& output, std::string& error);
    size_t capacity() const noexcept;
    size_t in_use() const noexcept;
    size_t peak_in_use() const noexcept;

 private:
    struct Slot { uint64_t generation = 0; bool in_use = false; };
    void recycle(size_t slot, uint64_t generation) noexcept;

    mutable std::mutex mutex_;
    std::vector<Slot> slots_;
    size_t in_use_ = 0;
    size_t peak_in_use_ = 0;
};

}  // namespace mkvc::gpu
