from __future__ import annotations

from .native import library as native
from .api.backend import _select_backend, backend_capabilities, select_backend
from ._cpu import BorrowedCpuFrame, CpuBuffer, CpuFramePool, Submission
from ._gpu import GpuFrame, GpuPlane
from ._intel_usm import IntelUsmFramePool, IntelUsmPoolSlot
from .api.capture import VideoCapture
from .api.writer import VideoWriter
from ._types import (
    BackendCapability,
    CpuFrame,
    CpuFramePoolStatistics,
    GpuInteropInfo,
    GpuResourcePoolStats,
    PipelineMetrics,
    CopyEdgeMetrics,
    PipelineComponentMetrics,
    PipelineStageMetrics,
    VideoInfo,
    U8Plane,
)
from ._video_info import probe_video

# Compatibility surface for source-tree GPU qualification scripts. New
# application code imports the stable names from ``mkvcodec``.
__all__ = [
    "BackendCapability",
    "BorrowedCpuFrame",
    "CopyEdgeMetrics",
    "CpuBuffer",
    "CpuFrame",
    "CpuFramePool",
    "CpuFramePoolStatistics",
    "GpuFrame",
    "GpuInteropInfo",
    "GpuPlane",
    "GpuResourcePoolStats",
    "IntelUsmFramePool",
    "IntelUsmPoolSlot",
    "PipelineComponentMetrics",
    "PipelineMetrics",
    "PipelineStageMetrics",
    "Submission",
    "U8Plane",
    "VideoCapture",
    "VideoInfo",
    "VideoWriter",
    "_select_backend",
    "backend_capabilities",
    "native",
    "probe_video",
    "select_backend",
]
