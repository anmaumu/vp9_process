"""Validation boundaries for Python external GPU frame imports."""

from __future__ import annotations


_UINT32_MAX = 0xFFFFFFFF
_UINT64_MAX = 0xFFFFFFFFFFFFFFFF
_INT64_MIN = -0x8000000000000000
_INT64_MAX = 0x7FFFFFFFFFFFFFFF


def frame_size(frame_size: tuple[int, int], *, integers: bool) -> tuple[int, int]:
    """Return width and height after validating the public tuple shape."""
    if not isinstance(frame_size, tuple) or len(frame_size) != 2:
        message = (
            "frame_size must contain integer width and height"
            if integers
            else ("frame_size must contain width and height")
        )
        raise ValueError(message)
    if integers and any(not isinstance(value, int) for value in frame_size):
        raise ValueError("frame_size must contain integer width and height")
    return frame_size


def validate_nv12_dimensions(width: int, height: int) -> None:
    """Require positive even NV12 dimensions for DLPack consumption."""
    if width <= 0 or height <= 0 or width & 1 or height & 1:
        raise ValueError("NV12 dimensions must be positive and even")


def validate_cuda_pointer(
    *,
    pointer: int,
    context: int,
    device_id: int,
    width: int,
    height: int,
    pitch: int,
    stream: int,
    event: int,
    pts_ns: int,
) -> None:
    """Validate the scalar CUDA pointer import descriptor."""
    values = (pointer, context, device_id, width, height, pitch, stream, event)
    if any(not isinstance(value, int) for value in values):
        raise ValueError("CUDA import descriptors must be integers")
    if (
        pointer <= 0
        or context <= 0
        or device_id < 0
        or width <= 0
        or height <= 0
        or width & 1
        or height & 1
        or pitch < width
        or stream < 0
        or event < 0
        or any(value > _UINT64_MAX for value in (pointer, context, device_id, pitch, stream, event))
        or width > _UINT32_MAX
        or height > _UINT32_MAX
        or pts_ns < _INT64_MIN
        or pts_ns > _INT64_MAX
    ):
        raise ValueError("CUDA import descriptor is invalid")


def validate_cuda_array(
    *,
    array: int,
    context: int,
    device_id: int,
    width: int,
    height: int,
    stream: int,
    event: int,
    pts_ns: int,
) -> None:
    """Validate the scalar CUDA array import descriptor."""
    values = (array, context, device_id, width, height, stream, event)
    if any(not isinstance(value, int) for value in values):
        raise ValueError("CUDA import descriptors must be integers")
    if (
        array <= 0
        or context <= 0
        or device_id < 0
        or width <= 0
        or height <= 0
        or width & 1
        or height & 1
        or stream < 0
        or event < 0
        or any(value > _UINT64_MAX for value in (array, context, device_id, stream, event))
        or width > _UINT32_MAX
        or height > _UINT32_MAX
        or pts_ns < _INT64_MIN
        or pts_ns > _INT64_MAX
    ):
        raise ValueError("CUDA array import descriptor is invalid")


def validate_d3d11(
    *,
    texture: int,
    fence: int,
    fence_value: int,
    device_id: int,
    width: int,
    height: int,
    pts_ns: int,
) -> None:
    """Validate the scalar D3D11 texture and fence import descriptor."""
    values = (texture, fence, fence_value, device_id, width, height, pts_ns)
    if any(not isinstance(value, int) for value in values):
        raise ValueError("D3D11 import descriptors must be integers")
    if (
        not 0 < texture <= _UINT64_MAX
        or not 0 < fence <= _UINT64_MAX
        or not 0 < fence_value < _UINT64_MAX
        or not 0 <= device_id <= _UINT64_MAX
        or not 0 < width <= _UINT32_MAX
        or not 0 < height <= _UINT32_MAX
        or width & 1
        or height & 1
        or not _INT64_MIN <= pts_ns <= _INT64_MAX
    ):
        raise ValueError("D3D11 import descriptor is invalid")


def validate_va_surface(
    *,
    display: int,
    surface_id: int,
    device_id: int,
    width: int,
    height: int,
    pts_ns: int,
) -> None:
    """Validate the scalar VA display and surface import descriptor."""
    values = (display, surface_id, device_id, width, height, pts_ns)
    if any(not isinstance(value, int) for value in values):
        raise ValueError("VA import descriptors must be integers")
    if (
        not 0 < display <= _UINT64_MAX
        or not 0 <= surface_id < _UINT32_MAX
        or not 0 <= device_id <= _UINT64_MAX
        or not 0 < width <= _UINT32_MAX
        or not 0 < height <= _UINT32_MAX
        or width & 1
        or height & 1
        or not _INT64_MIN <= pts_ns <= _INT64_MAX
    ):
        raise ValueError("VA import descriptor is invalid")


def validate_usm(
    *,
    pointer: int,
    context: int,
    queue: int,
    event: int,
    device_id: int,
    width: int,
    height: int,
    pitch: int,
    pts_ns: int,
) -> None:
    """Validate the scalar Intel USM import descriptor."""
    values = (pointer, context, queue, event, device_id, width, height, pitch, pts_ns)
    if any(not isinstance(value, int) for value in values):
        raise ValueError("USM import descriptors must be integers")
    if (
        pointer <= 0
        or context <= 0
        or queue <= 0
        or device_id < 0
        or width <= 0
        or height <= 0
        or width & 1
        or height & 1
        or pitch < width
        or pitch > _UINT32_MAX
        or event < 0
        or any(value > _UINT64_MAX for value in (pointer, context, queue, event, device_id))
        or width > _UINT32_MAX
        or height > _UINT32_MAX
        or pts_ns < _INT64_MIN
        or pts_ns > _INT64_MAX
    ):
        raise ValueError("USM import descriptor is invalid")
