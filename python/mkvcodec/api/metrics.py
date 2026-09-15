"""Immutable public pipeline and resource metric value types."""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class GpuInteropInfo:
    """Describe backend-neutral external processing compatibility."""

    backend: str
    memory_type: str
    native_handle_type: str
    processing_interfaces: tuple[str, ...]
    dlpack_export: bool
    completion: str
    api_stability: str


@dataclass(frozen=True)
class PipelineMetrics:
    """Report observed queue, backend, and copy-path counters."""

    accepted_frames: int
    completed_frames: int
    rejected_frames: int
    queue_wait_ns: int
    backend_time_ns: int
    queue_capacity: int
    peak_queue_depth: int
    hardware_pending_peak: int
    copy_path: str


@dataclass(frozen=True)
class PipelineStageMetrics:
    """Report host timings split by operation and execution path."""

    frame_calls: int
    frame_time_ns: int
    flush_calls: int
    flush_time_ns: int
    close_calls: int
    close_time_ns: int
    sync_frame_calls: int
    sync_frame_time_ns: int
    worker_frame_calls: int
    worker_frame_time_ns: int


@dataclass(frozen=True)
class PipelineComponentMetrics:
    """Report exclusive conversion, codec, container, and GPU-wait time."""

    conversion_calls: int
    conversion_time_ns: int
    codec_calls: int
    codec_time_ns: int
    container_calls: int
    container_time_ns: int
    gpu_wait_calls: int
    gpu_wait_time_ns: int


@dataclass(frozen=True)
class CopyEdgeMetrics:
    """Count copies and shares observed at mkvcodec-controlled boundaries."""

    shared_surface_frames: int
    zero_copy_frames: int
    gpu_copy_frames: int
    cpu_upload_frames: int
    cpu_readback_frames: int
    cpu_normalization_frames: int
    pixel_conversion_frames: int
    driver_internal_observed: bool


@dataclass(frozen=True)
class GpuResourcePoolStats:
    """Snapshot a fixed-capacity external GPU resource pool."""

    capacity: int
    in_use: int
    peak_in_use: int
    acquisitions: int
    rejected_acquisitions: int
    wait_ns: int


@dataclass(frozen=True)
class CpuFramePoolStatistics:
    """Report cumulative native CPU pool allocation and lease observations."""

    capacity: int
    in_use: int
    peak_in_use: int
    memory_mode: str
    allocation_bytes: int
    page_locked_bytes: int
    acquisitions: int
    rejected_acquisitions: int
    wait_ns: int
    lease_time_ns: int
    peak_lease_time_ns: int


__all__ = [
    "CopyEdgeMetrics",
    "CpuFramePoolStatistics",
    "GpuInteropInfo",
    "GpuResourcePoolStats",
    "PipelineComponentMetrics",
    "PipelineMetrics",
    "PipelineStageMetrics",
]
