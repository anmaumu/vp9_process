#pragma once

#include <vpl/mfxvideo.h>

#include <memory>
#include <string>

#include "mkvcodec/mkvc.h"

namespace mkvc::gpu {
class GpuFrameCore;
}

namespace mkvc::gpu::intel {

/**
 * @brief Import a D3D11 or VA-API frame into a oneVPL encode session.
 *
 * @param session Destination oneVPL session.
 * @param frame Borrowed frame whose native resource remains leased by the caller.
 * @param surface Receives a referenced oneVPL surface on success.
 * @param error Receives a diagnostic on failure.
 * @return MKVC_OK when a shared, non-copying import was created.
 *
 * The caller owns the returned surface reference and must release it through
 * `FrameInterface->Release`. Import is rejected if the runtime reports a copy.
 */
mkvc_result import_vpl_encode_surface(mfxSession session,
                                      const std::shared_ptr<GpuFrameCore>& frame,
                                      mfxFrameSurface1*& surface, std::string& error);

}  // namespace mkvc::gpu::intel
