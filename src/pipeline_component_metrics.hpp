#pragma once

/**
 * @file pipeline_component_metrics.hpp
 * @brief Thread-local exclusive component timing for active pipeline calls.
 */

#include <atomic>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

#include "mkvcodec/mkvc.h"

namespace mkvc {

/** Internal timing categories exposed by mkvc_pipeline_component_metrics. */
enum class PipelineComponent { kConversion, kCodec, kContainer, kGpuWait };

/** Lock-free accumulator safe to snapshot while a worker is active. */
class ComponentMetricsAccumulator {
   public:
    /** Add one exclusive duration sample to a component. */
    void add(PipelineComponent component, uint64_t elapsed_ns) noexcept {
        counters_[static_cast<size_t>(component)].calls.fetch_add(1, std::memory_order_relaxed);
        counters_[static_cast<size_t>(component)].time_ns.fetch_add(elapsed_ns,
                                                                    std::memory_order_relaxed);
    }

    /** Return a coherent-enough cumulative telemetry snapshot. */
    mkvc_pipeline_component_metrics snapshot() const noexcept {
        mkvc_pipeline_component_metrics result{};
        result.struct_size = sizeof(result);
        result.struct_version = 1;
        load(0, result.conversion_calls, result.conversion_time_ns);
        load(1, result.codec_calls, result.codec_time_ns);
        load(2, result.container_calls, result.container_time_ns);
        load(3, result.gpu_wait_calls, result.gpu_wait_time_ns);
        return result;
    }

   private:
    struct Counter {
        std::atomic<uint64_t> calls{0};
        std::atomic<uint64_t> time_ns{0};
    };
    Counter counters_[4];

    void load(size_t index, uint64_t& calls, uint64_t& time_ns) const noexcept {
        calls = counters_[index].calls.load(std::memory_order_relaxed);
        time_ns = counters_[index].time_ns.load(std::memory_order_relaxed);
    }
};

inline thread_local ComponentMetricsAccumulator* active_component_metrics = nullptr;

namespace detail {

using ComponentClock = std::chrono::steady_clock;

struct ComponentTimerEntry {
    ComponentMetricsAccumulator* accumulator = nullptr;
    PipelineComponent component = PipelineComponent::kConversion;
    ComponentClock::time_point started{};
    uint64_t elapsed_ns = 0;
    bool running = false;
};

// Pipeline component scopes are shallow in normal operation. Keeping their
// mutable state in thread-local storage avoids retaining pointers to automatic
// ScopedComponentTimer objects and makes the LIFO lifetime explicit to GCC.
struct ComponentTimerStack {
    static constexpr size_t kCapacity = 32;
    std::array<ComponentTimerEntry, kCapacity> entries{};
    size_t depth = 0;
};

inline thread_local ComponentTimerStack component_timer_stack;

inline uint64_t component_duration(ComponentClock::time_point begin,
                                   ComponentClock::time_point end) noexcept {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count());
}

inline void pause_component_timer(ComponentTimerEntry& entry,
                                  ComponentClock::time_point now) noexcept {
    if (!entry.running) return;
    entry.elapsed_ns += component_duration(entry.started, now);
    entry.running = false;
}

inline void resume_component_timer(ComponentTimerEntry& entry,
                                   ComponentClock::time_point now) noexcept {
    entry.started = now;
    entry.running = true;
}

}  // namespace detail

/** Bind component timers on the current thread to one pipeline accumulator. */
class ActiveComponentMetrics {
   public:
    explicit ActiveComponentMetrics(ComponentMetricsAccumulator& accumulator) noexcept
        : previous_(active_component_metrics) {
        active_component_metrics = &accumulator;
    }
    ~ActiveComponentMetrics() { active_component_metrics = previous_; }
    ActiveComponentMetrics(const ActiveComponentMetrics&) = delete;
    ActiveComponentMetrics& operator=(const ActiveComponentMetrics&) = delete;

   private:
    ComponentMetricsAccumulator* previous_;
};

/** Measure exclusive wall time, pausing an enclosing component timer. */
class ScopedComponentTimer {
   public:
    explicit ScopedComponentTimer(PipelineComponent component) noexcept {
        auto& stack = detail::component_timer_stack;
        if (active_component_metrics == nullptr || stack.depth == stack.kCapacity) return;
        const auto now = detail::ComponentClock::now();
        if (stack.depth != 0) {
            detail::pause_component_timer(stack.entries[stack.depth - 1], now);
        }
        index_ = stack.depth++;
        stack.entries[index_] = {active_component_metrics, component, now, 0, true};
        active_ = true;
    }

    ~ScopedComponentTimer() {
        if (!active_) return;
        auto& stack = detail::component_timer_stack;
        auto& entry = stack.entries[index_];
        const auto now = detail::ComponentClock::now();
        detail::pause_component_timer(entry, now);
        entry.accumulator->add(entry.component, entry.elapsed_ns);
        stack.depth = index_;
        if (stack.depth != 0) {
            detail::resume_component_timer(stack.entries[stack.depth - 1], now);
        }
    }

    ScopedComponentTimer(const ScopedComponentTimer&) = delete;
    ScopedComponentTimer& operator=(const ScopedComponentTimer&) = delete;

   private:
    size_t index_ = 0;
    bool active_ = false;
};

}  // namespace mkvc
