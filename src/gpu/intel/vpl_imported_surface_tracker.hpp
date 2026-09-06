/**
 * @file vpl_imported_surface_tracker.hpp
 * @brief Lifetime tracking for GPU frames imported into oneVPL encode sessions.
 */
#pragma once

#include <vpl/mfxvideo.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "mkvcodec/mkvc.h"

namespace mkvc::gpu {

class GpuFrameCore;

namespace intel {

/**
 * @brief Retain and retire external oneVPL encode-surface wrappers safely.
 *
 * The tracker validates D3D11-device or VA-display identity, waits for producer
 * completion without copying pixels, imports non-oneVPL GPU frames, and retains
 * their opaque leases until the runtime no longer owns the wrapper surface.
 */
class VplImportedSurfaceTracker final {
   public:
    /** Create a tracker bound to one encode session and optional external device. */
    VplImportedSurfaceTracker(mfxSession session, bool enforce_device_identity,
                              uint64_t device_identity) noexcept;
    ~VplImportedSurfaceTracker();
    VplImportedSurfaceTracker(const VplImportedSurfaceTracker&) = delete;
    VplImportedSurfaceTracker& operator=(const VplImportedSurfaceTracker&) = delete;

    /**
     * @brief Wait for and acquire a oneVPL surface representing frame.
     * @param frame Compatible Intel NV12 GPU frame.
     * @param surface Receives a borrowed internal surface or retained import wrapper.
     * @param imported Reports whether a private wrapper was created.
     * @param error Receives synchronization/import diagnostics.
     */
    mkvc_result acquire(const std::shared_ptr<GpuFrameCore>& frame, mfxFrameSurface1*& surface,
                        bool& imported, std::string& error);

    /** Retire wrappers whose oneVPL runtime references have drained. */
    void retire();

    /** Release wrapper references while the encode component still exists. */
    void release_wrappers_before_runtime_close() noexcept;

    /** Release original frame leases after the runtime has been destroyed. */
    void release_owners_after_runtime_close() noexcept;

   private:
    struct ImportedInput;
    bool validate_device(const GpuFrameCore& frame, std::string& error) const;

    mfxSession session_ = nullptr;
    bool enforce_device_identity_ = false;
    uint64_t device_identity_ = 0;
    std::vector<std::unique_ptr<ImportedInput>> inputs_;
};

}  // namespace intel
}  // namespace mkvc::gpu
