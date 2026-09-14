/**
 * @file cpu_vp9_decoder_threads.hpp
 * @brief Bounded automatic thread selection for the CPU VP9 decoder.
 */
#pragma once

#include <algorithm>
#include <cstdint>
#include <thread>

namespace mkvc::cpu_vp9_decoder_detail {

/** Maximum libvpx decoder threads selected by the automatic policy. */
inline constexpr uint32_t kAutomaticThreadLimit = 16;

/**
 * @brief Resolve the public decoder thread setting for libvpx.
 *
 * An explicit nonzero value is preserved. Zero means automatic selection and
 * is bounded because libvpx VP9 decode stops scaling before very large logical
 * CPU counts while color conversion and prefetch also require CPU time.
 *
 * @param requested Public thread setting; zero requests automatic selection.
 * @param hardware_threads Detected logical CPU count; zero means unavailable.
 * @return Positive libvpx decoder thread count.
 */
constexpr uint32_t resolve_thread_count(const uint32_t requested,
                                        const uint32_t hardware_threads) noexcept {
    if (requested != 0) return requested;
    return std::min(kAutomaticThreadLimit, std::max(1u, hardware_threads));
}

/** Resolve the public setting using the current process hardware concurrency. */
inline uint32_t resolve_thread_count(const uint32_t requested) noexcept {
    return resolve_thread_count(requested, std::thread::hardware_concurrency());
}

}  // namespace mkvc::cpu_vp9_decoder_detail
