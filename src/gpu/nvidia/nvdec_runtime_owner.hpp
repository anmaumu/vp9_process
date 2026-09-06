#pragma once

#include <ffnvcodec/dynlink_cuda.h>
#include <ffnvcodec/dynlink_cuviddec.h>
#include <ffnvcodec/dynlink_nvcuvid.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include "nvdec_runtime_setup.hpp"

namespace mkvc::gpu::nvidia {

/**
 * @brief Own NVDEC parser/runtime resources across exported mapped-frame leases.
 *
 * Close destroys the parser immediately so no new frames can be produced. Decoder
 * and CUDA-context destruction is deferred until every mapped output lease calls
 * `release_mapping`. Each release callback retains this owner, so its native fields
 * remain valid even after the public decoder object has been destroyed.
 */
class NvdecRuntimeOwner final {
   public:
    /** Adopt a successfully created parser runtime. */
    explicit NvdecRuntimeOwner(NvdecParserRuntime runtime);
    ~NvdecRuntimeOwner();
    NvdecRuntimeOwner(const NvdecRuntimeOwner&) = delete;
    NvdecRuntimeOwner& operator=(const NvdecRuntimeOwner&) = delete;

    /** Borrow the loaded driver table for parser callbacks and output adapters. */
    NvdecApi& api() noexcept { return *api_; }

    /** Borrow the CUDA context while this owner or an output lease is alive. */
    CUcontext context() const noexcept { return context_; }

    /** Borrow the parser used by the single decoder read thread. */
    CUvideoparser parser() const noexcept { return parser_; }

    /** Mutable decoder handle populated by the sequence callback. */
    CUvideodecoder& decoder() noexcept { return decoder_; }

    /** Record a mapped frame after its output lease has been constructed. */
    void acquire_mapping();

    /** Unmap one frame and perform deferred runtime destruction when last. */
    void release_mapping(unsigned long long device_pointer) noexcept;

    /** Destroy the parser and defer or perform decoder/context destruction. */
    bool close(std::string& error) noexcept;

    /** Return the number of mapped frames whose leases remain alive. */
    uint32_t outstanding_mappings() const noexcept;

   private:
    bool destroy_decoder_if_ready(std::string& error) noexcept;

    std::unique_ptr<NvdecApi> api_;
    CUcontext context_ = nullptr;
    CUvideoparser parser_ = nullptr;
    CUvideodecoder decoder_ = nullptr;
    mutable std::mutex mutex_;
    uint32_t outstanding_mappings_ = 0;
    bool closed_ = false;
};

}  // namespace mkvc::gpu::nvidia
