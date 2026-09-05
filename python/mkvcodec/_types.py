"""Public immutable value types shared by the Python API modules."""
from __future__ import annotations

from dataclasses import dataclass

import numpy as np
import numpy.typing as npt

U8Plane = npt.NDArray[np.uint8]


@dataclass(frozen=True)
class CpuFrame:
    """Owned decoded I420 image.

    Attributes
    ----------
    y, u, v:
        Owned ``uint8`` planes. Chroma planes have half width and height.
    pts_ns:
        Presentation timestamp in nanoseconds.
    """

    y: U8Plane
    u: U8Plane
    v: U8Plane
    pts_ns: int


@dataclass(frozen=True)
class BackendCapability:
    """One runtime-supported codec direction.

    Attributes
    ----------
    backend:
        ``"cpu"``, ``"intel"`` or ``"nvidia"``.
    codec:
        ``"vp9"`` or ``"av1"``.
    can_decode, can_encode:
        Whether the runtime can execute the corresponding direction.
    is_hardware:
        Whether frames can remain on a GPU backend.
    """

    backend: str
    codec: str
    can_decode: bool
    can_encode: bool
    is_hardware: bool


@dataclass(frozen=True)
class GpuInteropInfo:
    """Backend-neutral external processing compatibility.

    Attributes
    ----------
    backend:
        Backend owning the frame.
    memory_type:
        Concrete storage representation.
    native_handle_type:
        Borrowed native handle representation.
    processing_interfaces:
        Adapter families that may consume the representation.
    dlpack_export:
        Whether linear planes support the DLPack protocol.
    completion:
        Producer-completion mechanism associated with the frame.
    """

    backend: str
    memory_type: str
    native_handle_type: str
    processing_interfaces: tuple[str, ...]
    dlpack_export: bool
    completion: str


@dataclass(frozen=True)
class PipelineMetrics:
    """Observed queue, backend and copy-path metrics.

    Attributes
    ----------
    accepted_frames, completed_frames, rejected_frames:
        Frame counters at the public session boundary.
    queue_wait_ns, backend_time_ns:
        Accumulated queue and codec execution time in nanoseconds.
    queue_capacity, peak_queue_depth:
        Configured and observed host queue bounds.
    hardware_pending_peak:
        Highest backend-owned in-flight frame count.
    copy_path:
        ``"unknown"``, ``"cpu"``, ``"zero_copy"`` or ``"mixed"``.
    """

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
class GpuResourcePoolStats:
    """Snapshot of a fixed-capacity external GPU resource pool.

    Attributes
    ----------
    capacity, in_use, peak_in_use:
        Configured, current and peak slot occupancy.
    acquisitions, rejected_acquisitions:
        Successful and backpressured/timed-out acquisition counts.
    wait_ns:
        Total time spent waiting for a slot in nanoseconds.
    """

    capacity: int
    in_use: int
    peak_in_use: int
    acquisitions: int
    rejected_acquisitions: int
    wait_ns: int
