"""Native descriptor, handle, and completion access for GPU frame leases."""

from __future__ import annotations

import ctypes as ct

from . import _native as native


def get_gpu_frame_descriptor(handle: native.GpuFrameHandle) -> dict[str, object]:
    """Return normalized metadata for an open native GPU frame handle."""
    value = native.GpuFrameDesc()
    value.struct_size = ct.sizeof(value)
    value.struct_version = 1
    native.check(native.lib.mkvc_gpu_frame_get_desc(handle, ct.byref(value)))
    return {
        "backend": value.backend,
        "memory_type": value.memory_type,
        "device_id": value.device_id,
        "generation": value.generation,
        "pixel_format": value.pixel_format,
        "width": value.width,
        "height": value.height,
        "plane_count": value.plane_count,
        "plane_offsets": tuple(value.plane_offsets),
        "pitches": tuple(value.pitches),
        "pts_ns": value.pts,
    }


def get_gpu_native_handle(handle: native.GpuFrameHandle) -> dict[str, object]:
    """Return normalized borrowed resource handles for an open GPU frame."""
    value = native.GpuNativeHandleDesc()
    value.struct_size = ct.sizeof(value)
    value.struct_version = 1
    native.check(native.lib.mkvc_gpu_frame_get_native_handle(handle, ct.byref(value)))
    return {
        "type": value.type,
        "borrowed": bool(value.borrowed),
        "device_id": value.device_id,
        "generation": value.generation,
        "handles": tuple(value.handles),
    }


def wait_gpu_frame(handle: native.GpuFrameHandle, timeout_ms: int) -> None:
    """Validate a timeout and wait for native producer completion."""
    if timeout_ms < 0 or timeout_ms > 0xFFFFFFFF:
        raise ValueError("timeout_ms is outside uint32 range")
    native.check(native.lib.mkvc_gpu_frame_wait(handle, timeout_ms))
