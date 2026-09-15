from __future__ import annotations

from .native import library as native
from .api.backend import _select_backend, backend_capabilities, select_backend
from .internal.cpu_pool import BorrowedCpuFrame, CpuBuffer, CpuFramePool
from .internal.submission import Submission
from .api.frame import GpuFrame, GpuPlane
from .internal.intel_usm_pool import IntelUsmFramePool, IntelUsmPoolSlot
from .api.capture import VideoCapture
from .api.writer import VideoWriter
from .api.backend import BackendCapability
from .api.frame import CpuFrame, U8Plane
from .api.metrics import (
    CpuFramePoolStatistics,
    GpuInteropInfo,
    GpuResourcePoolStats,
    PipelineMetrics,
    CopyEdgeMetrics,
    PipelineComponentMetrics,
    PipelineStageMetrics,
)
from .api.video import VideoInfo, probe_video

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
