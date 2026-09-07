/**
 * @file cpu_conversion_workers.hpp
 * @brief Bounded persistent workers for large CPU color conversions.
 */
#pragma once

#include <cstddef>
#include <functional>
#include <future>

namespace mkvc {

/** Return the caller plus worker count available to one parallel conversion. */
size_t cpu_conversion_parallelism();

/** Submit one independent conversion stripe to the bounded shared worker pool. */
std::future<int> submit_cpu_conversion(std::function<int()> operation);

}  // namespace mkvc
