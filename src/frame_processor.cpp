#include "frame_processor.hpp"

#include <algorithm>
#include <cstdint>

#include "frame_processor_i420.hpp"

namespace mkvc {

mkvc_result process_frame_cpu(const DecodedFrame& source, const mkvc_frame_process_config& config,
                              std::unique_ptr<DecodedFrame>& output, std::string& error) {
#if !defined(MKVC_HAS_CPU_VP9) && !defined(MKVC_HAS_CPU_AV1) && !defined(MKVC_HAS_INTEL_ONEVPL) && \
    !defined(MKVC_HAS_NVIDIA)
    (void)source;
    (void)config;
    (void)output;
    error = "frame processing was not built";
    return MKVC_ERROR_NOT_SUPPORTED;
#else
    namespace detail = frame_processor_detail;
    output.reset();
    if (config.backend != MKVC_BACKEND_CPU) {
        error = "requested GPU frame processor is not implemented";
        return MKVC_ERROR_NOT_SUPPORTED;
    }
    if ((config.rotation != 0 && config.rotation != 90 && config.rotation != 180 &&
         config.rotation != 270) ||
        config.fit > MKVC_FRAME_FIT_COVER) {
        error = "invalid rotation or fit policy";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    const uint32_t crop_width = config.crop_width == 0 ? source.width : config.crop_width;
    const uint32_t crop_height = config.crop_height == 0 ? source.height : config.crop_height;
    if (!detail::even_nonzero(crop_width) || !detail::even_nonzero(crop_height) ||
        (config.crop_x & 1u) || (config.crop_y & 1u) ||
        config.crop_x > source.width - std::min(source.width, crop_width) ||
        config.crop_y > source.height - std::min(source.height, crop_height) ||
        crop_width > source.width - config.crop_x || crop_height > source.height - config.crop_y) {
        error = "crop rectangle must be even, nonempty, and inside the frame";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }

    auto current =
        detail::crop_frame(source, config.crop_x, config.crop_y, crop_width, crop_height);
    if (!current) {
        error = "crop failed";
        return MKVC_ERROR_INTERNAL;
    }
    if (config.rotation != 0) {
        current = detail::rotate_frame(*current, config.rotation);
        if (!current) {
            error = "rotation failed";
            return MKVC_ERROR_INTERNAL;
        }
    }
    if (config.flip_horizontal) {
        current = detail::mirror_frame(*current);
        if (!current) {
            error = "horizontal flip failed";
            return MKVC_ERROR_INTERNAL;
        }
    }
    if (config.flip_vertical) {
        auto rotated = detail::rotate_frame(*current, 180);
        current = rotated ? detail::mirror_frame(*rotated) : nullptr;
        if (!current) {
            error = "vertical flip failed";
            return MKVC_ERROR_INTERNAL;
        }
    }

    const uint32_t out_width = config.output_width == 0 ? current->width : config.output_width;
    const uint32_t out_height = config.output_height == 0 ? current->height : config.output_height;
    if (!detail::even_nonzero(out_width) || !detail::even_nonzero(out_height)) {
        error = "output dimensions must be positive and even";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    if (config.fit == MKVC_FRAME_FIT_STRETCH) {
        output = (current->width == out_width && current->height == out_height)
                     ? std::move(current)
                     : detail::scale_frame(*current, out_width, out_height);
    } else {
        const bool contain = config.fit == MKVC_FRAME_FIT_CONTAIN;
        const uint64_t lhs = static_cast<uint64_t>(out_width) * current->height;
        const uint64_t rhs = static_cast<uint64_t>(out_height) * current->width;
        uint32_t scaled_width;
        uint32_t scaled_height;
        if ((contain && lhs <= rhs) || (!contain && lhs >= rhs)) {
            scaled_width = out_width;
            scaled_height = detail::even_floor(static_cast<uint64_t>(current->height) * out_width /
                                               current->width);
        } else {
            scaled_height = out_height;
            scaled_width = detail::even_floor(static_cast<uint64_t>(current->width) * out_height /
                                              current->height);
        }
        scaled_width = std::max(2u, scaled_width);
        scaled_height = std::max(2u, scaled_height);
        auto scaled = detail::scale_frame(*current, scaled_width, scaled_height);
        if (!scaled) {
            error = "aspect-preserving resize failed";
            return MKVC_ERROR_INTERNAL;
        }
        output = detail::allocate_i420(out_width, out_height, source.pts_ns);
        if (!output) {
            error = "output allocation failed";
            return MKVC_ERROR_INTERNAL;
        }
        if (contain) {
            detail::fill_background(*output, config.background_rgba);
            const uint32_t x = detail::even_floor((out_width - scaled_width) / 2);
            const uint32_t y = detail::even_floor((out_height - scaled_height) / 2);
            if (detail::copy_i420(*scaled, *output, 0, 0, x, y) != 0) output.reset();
        } else {
            const uint32_t x = detail::even_floor((scaled_width - out_width) / 2);
            const uint32_t y = detail::even_floor((scaled_height - out_height) / 2);
            if (detail::copy_i420(*scaled, *output, x, y) != 0) output.reset();
        }
    }
    if (!output) {
        error = "frame resize/composition failed";
        return MKVC_ERROR_INTERNAL;
    }
    return MKVC_OK;
#endif
}

}  // namespace mkvc
