#include <cassert>

#include "pipeline_component_metrics.hpp"

namespace {

void nested_timing(mkvc::ComponentMetricsAccumulator& child) {
    mkvc::ScopedComponentTimer codec(mkvc::PipelineComponent::kCodec);
    {
        mkvc::ActiveComponentMetrics use_child(child);
        mkvc::ScopedComponentTimer conversion(mkvc::PipelineComponent::kConversion);
    }
    {
        mkvc::ScopedComponentTimer container(mkvc::PipelineComponent::kContainer);
    }
}

}  // namespace

int main() {
    mkvc::ComponentMetricsAccumulator parent;
    mkvc::ComponentMetricsAccumulator child;

    // A nested accumulator temporarily pauses the outer timer and restores it
    // when the inner scope ends.
    {
        mkvc::ActiveComponentMetrics use_parent(parent);
        nested_timing(child);
        mkvc::ScopedComponentTimer wait(mkvc::PipelineComponent::kGpuWait);
    }

    const auto parent_snapshot = parent.snapshot();
    assert(parent_snapshot.codec_calls == 1);
    assert(parent_snapshot.container_calls == 1);
    assert(parent_snapshot.gpu_wait_calls == 1);
    assert(parent_snapshot.conversion_calls == 0);

    const auto child_snapshot = child.snapshot();
    assert(child_snapshot.conversion_calls == 1);
    assert(child_snapshot.codec_calls == 0);

    // Timers outside an active pipeline accumulator are intentionally inert.
    { mkvc::ScopedComponentTimer ignored(mkvc::PipelineComponent::kCodec); }
    assert(parent.snapshot().codec_calls == 1);
    return 0;
}
