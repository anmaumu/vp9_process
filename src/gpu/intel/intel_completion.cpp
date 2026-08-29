#include "intel_completion.hpp"

#include <string>
#include <utility>

namespace mkvc::gpu::intel {

std::shared_ptr<Completion> make_sync_point_completion(
    mfxSession session, mfxSyncPoint sync_point,
    std::shared_ptr<void> session_keepalive) {
    return std::make_shared<CallbackCompletion>(
        [session, sync_point, keepalive = std::move(session_keepalive)](
            bool& complete, std::string& error) {
            (void)keepalive;
            if (session == nullptr || sync_point == nullptr) {
                error = "invalid oneVPL session or SyncPoint";
                return MKVC_ERROR_INVALID_ARGUMENT;
            }
            const mfxStatus status = MFXVideoCORE_SyncOperation(session, sync_point, 0);
            if (status == MFX_ERR_NONE) { complete = true; return MKVC_OK; }
            if (status == MFX_WRN_IN_EXECUTION) { complete = false; return MKVC_OK; }
            error = "oneVPL SyncOperation failed with status " + std::to_string(status);
            return status == MFX_ERR_DEVICE_LOST ? MKVC_ERROR_IO : MKVC_ERROR_CODEC;
        });
}

}  // namespace mkvc::gpu::intel
