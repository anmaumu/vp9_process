#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "mkvcodec/mkvc.h"

namespace mkvc::encoder {

/**
 * @brief Packed ownership of one CPU frame accepted by the asynchronous queue.
 *
 * Source row padding is removed while copying. The object owns all pixel bytes
 * referenced by the view returned from view().
 */
struct OwnedFrame {
    uint32_t pixel_format = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    int64_t pts = -1;
    std::array<std::vector<uint8_t>, 3> planes;
    std::array<int32_t, 3> strides{};

    /** @brief Return a non-owning C ABI view valid for this object's lifetime. */
    mkvc_frame_view view() const;
};

/**
 * @brief Validate a CPU frame that remains owned by the caller.
 * @param frame Candidate borrowed frame descriptor.
 * @param error Receives an actionable validation diagnostic.
 * @return MKVC_OK when every required plane and stride is valid.
 */
mkvc_result validate_borrowed_frame(const mkvc_frame_view& frame, std::string& error);

/**
 * @brief Deep-copy a CPU frame into queue-owned packed storage.
 * @param source Candidate source frame descriptor.
 * @param destination Receives owned plane bytes and metadata.
 * @param error Receives an actionable validation diagnostic.
 * @return MKVC_OK on success or a stable validation result.
 */
mkvc_result own_frame(const mkvc_frame_view& source, OwnedFrame& destination, std::string& error);

}  // namespace mkvc::encoder
