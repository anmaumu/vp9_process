"""Stable high-level Python API.

Application code should import these names from :mod:`mkvcodec`.  The modules
in this package group the public surface by responsibility without exposing
the ctypes implementation.
"""

from .backend import BackendCapability, backend_capabilities, select_backend
from .frame import (
    BorrowedCpuFrame,
    CpuBuffer,
    CpuFrame,
    CpuFramePool,
    GpuFrame,
    GpuPlane,
    IntelUsmFramePool,
    IntelUsmPoolSlot,
    Submission,
)
from .capture import VideoCapture
from .metrics import (
    CopyEdgeMetrics,
    CpuFramePoolStatistics,
    GpuInteropInfo,
    GpuResourcePoolStats,
    PipelineComponentMetrics,
    PipelineMetrics,
    PipelineStageMetrics,
)
from .video import VideoInfo, probe_video
from .writer import VideoWriter

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
    "VideoCapture",
    "VideoInfo",
    "VideoWriter",
    "backend_capabilities",
    "probe_video",
    "select_backend",
]
