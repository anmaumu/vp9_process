#pragma once

/**
 * @file pipeline_component_metrics.hpp
 * @brief Thread-local exclusive component timing for active pipeline calls.
 */

#include <atomic>
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
    explicit ScopedComponentTimer(PipelineComponent component) noexcept
        : accumulator_(active_component_metrics), component_(component), parent_(current_) {
        if (accumulator_ == nullptr) return;
        const auto now = Clock::now();
        if (parent_ != nullptr) parent_->pause(now);
        started_ = now;
        running_ = true;
        current_ = this;
    }

    ~ScopedComponentTimer() {
        if (!running_) return;
        const auto now = Clock::now();
        elapsed_ns_ += duration(started_, now);
        accumulator_->add(component_, elapsed_ns_);
        current_ = parent_;
        if (parent_ != nullptr) parent_->resume(now);
    }

    ScopedComponentTimer(const ScopedComponentTimer&) = delete;
    ScopedComponentTimer& operator=(const ScopedComponentTimer&) = delete;

   private:
    using Clock = std::chrono::steady_clock;
    ComponentMetricsAccumulator* accumulator_ = nullptr;
    PipelineComponent component_;
    ScopedComponentTimer* parent_ = nullptr;
    Clock::time_point started_{};
    uint64_t elapsed_ns_ = 0;
    bool running_ = false;
    inline static thread_local ScopedComponentTimer* current_ = nullptr;

    static uint64_t duration(Clock::time_point begin, Clock::time_point end) noexcept {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count());
    }
    void pause(Clock::time_point now) noexcept {
        if (!running_) return;
        elapsed_ns_ += duration(started_, now);
        running_ = false;
    }
    void resume(Clock::time_point now) noexcept {
        started_ = now;
        running_ = true;
    }
};

}  // namespace mkvc
