#include "encoder/encoder_c_api_support.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>

#include "backend_registry.hpp"
#include "encoder_session.hpp"

namespace mkvc::encoder::capi {

bool valid_encoder_config(const mkvc_encoder_config* config) noexcept {
    return config != nullptr && config->struct_size >= sizeof(mkvc_encoder_config) &&
           config->struct_version == 1 && config->output_path_utf8 != nullptr &&
           config->output_path_utf8[0] != '\0' &&
           (config->codec == MKVC_CODEC_VP9 || config->codec == MKVC_CODEC_AV1) &&
           (config->backend == MKVC_BACKEND_CPU || config->backend == MKVC_BACKEND_INTEL ||
            config->backend == MKVC_BACKEND_NVIDIA) &&
           config->width != 0 && config->height != 0 && (config->width & 1u) == 0 &&
           (config->height & 1u) == 0 && config->fps_num != 0 && config->fps_den != 0 &&
           config->quality <= 63 &&
           config->width <= static_cast<uint32_t>(std::numeric_limits<int32_t>::max() / 4) &&
           config->height <= static_cast<uint32_t>(std::numeric_limits<int32_t>::max());
}

bool valid_frame_view(const mkvc_frame_view* frame) noexcept {
    return frame != nullptr && frame->struct_size >= sizeof(mkvc_frame_view) &&
           frame->struct_version == 1;
}

mkvc_result create_encoder_session(const mkvc_encoder_config& config,
                                   std::unique_ptr<EncoderSession>& session, std::string& error) {
    if (config.backend == MKVC_BACKEND_INTEL ||
        config.backend == MKVC_BACKEND_NVIDIA) {
        const auto& capabilities = backend_capabilities();
        const bool available =
            std::any_of(capabilities.begin(), capabilities.end(), [&config](const auto& item) {
                return item.backend == config.backend && item.codec == config.codec &&
                       item.can_encode != 0;
            });
        if (!available) {
            error = "requested hardware encode capability is unavailable";
            return MKVC_ERROR_NOT_SUPPORTED;
        }
    }
    session = EncoderSession::create(config, error);
    return session ? MKVC_OK : MKVC_ERROR_CODEC;
}

}  // namespace mkvc::encoder::capi
