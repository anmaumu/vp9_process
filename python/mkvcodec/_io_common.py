"""Shared native-view helpers for capture and writer implementations."""

from __future__ import annotations

import ctypes as ct
from fractions import Fraction

from . import _native as native
from ._types import PipelineMetrics, U8Plane


def _read_metrics(handle: ct.c_void_p, function: object) -> PipelineMetrics:
    metrics = native.PipelineMetrics()
    metrics.struct_size = ct.sizeof(metrics)
    metrics.struct_version = 1
    native.check(function(handle, ct.byref(metrics)))
    paths = {0: "unknown", 1: "cpu", 2: "zero_copy", 3: "mixed"}
    return PipelineMetrics(
        accepted_frames=metrics.accepted_frames,
        completed_frames=metrics.completed_frames,
        rejected_frames=metrics.rejected_frames,
        queue_wait_ns=metrics.queue_wait_ns,
        backend_time_ns=metrics.backend_time_ns,
        queue_capacity=metrics.queue_capacity,
        peak_queue_depth=metrics.peak_queue_depth,
        hardware_pending_peak=metrics.hardware_pending_peak,
        copy_path=paths.get(metrics.copy_path, f"unknown_{metrics.copy_path}"),
    )


def _fps_fraction(fps: float | int | tuple[int, int]) -> Fraction:
    if isinstance(fps, tuple):
        value = Fraction(*fps)
    else:
        value = Fraction(fps).limit_denominator(100_000)
    if value <= 0:
        raise ValueError("fps must be positive")
    return value


def _plane_pointer(array: U8Plane) -> ct.POINTER(ct.c_uint8):
    return ct.cast(int(array.ctypes.data), ct.POINTER(ct.c_uint8))
