"""Validate NumPy frame layouts and build native CPU frame views."""

from __future__ import annotations

import ctypes as ct

import numpy as np

from ..native import library as native
from .io_common import _plane_pointer
from ..api.frame import U8Plane


FrameInput = U8Plane | tuple[U8Plane, U8Plane] | tuple[U8Plane, U8Plane, U8Plane]


def _build_view(
    planes: tuple[U8Plane, ...],
    *,
    pixel_format: int,
    width: int,
    height: int,
    pts: int,
) -> native.FrameView:
    """Build a native view whose storage remains owned by ``planes``."""
    view = native.FrameView()
    view.struct_size = ct.sizeof(view)
    view.struct_version = 1
    view.pixel_format = pixel_format
    view.width = width
    view.height = height
    view.pts = pts
    for index, plane in enumerate(planes):
        view.planes[index] = _plane_pointer(plane)
        view.strides[index] = plane.strides[0]
    view._plane_owners = planes
    return view


def _validate_planes(
    planes: tuple[U8Plane, ...],
    expected: tuple[tuple[int, ...], ...],
    *,
    label: str,
    allow_copy: bool,
) -> tuple[U8Plane, ...]:
    """Reject layouts that the native row-stride ABI cannot represent."""
    normalized: list[U8Plane] = []
    for plane, shape in zip(planes, expected):
        if plane.dtype != np.uint8 or plane.shape != shape:
            raise ValueError(f"{label} plane must be uint8 with shape {shape}")
        valid = plane.strides[0] > 0 and plane.strides[-1] == 1
        valid = valid and (plane.ndim != 3 or plane.strides[1] == plane.shape[2])
        if not valid:
            if not allow_copy:
                raise ValueError(f"{label} planes require positive packed element stride")
            plane = np.ascontiguousarray(plane)
        normalized.append(plane)
    return tuple(normalized)


def make_i420_view(
    y: U8Plane,
    u: U8Plane,
    v: U8Plane,
    *,
    width: int,
    height: int,
    pts: int,
    allow_copy: bool = False,
) -> native.FrameView:
    """Validate an I420 plane tuple and return its borrowed native view."""
    planes = tuple(np.asarray(plane) for plane in (y, u, v))
    expected = (
        (height, width),
        (height // 2, width // 2),
        (height // 2, width // 2),
    )
    planes = _validate_planes(planes, expected, label="I420", allow_copy=allow_copy)
    return _build_view(
        planes,
        pixel_format=native.MKVC_PIXEL_FORMAT_I420,
        width=width,
        height=height,
        pts=pts,
    )


def make_nv12_view(
    y: U8Plane,
    uv: U8Plane,
    *,
    width: int,
    height: int,
    pts: int,
    allow_copy: bool = False,
) -> native.FrameView:
    """Validate an NV12 plane tuple and return its borrowed native view."""
    planes = tuple(np.asarray(plane) for plane in (y, uv))
    expected = ((height, width), (height // 2, width))
    planes = _validate_planes(planes, expected, label="NV12", allow_copy=allow_copy)
    return _build_view(
        planes,
        pixel_format=native.MKVC_PIXEL_FORMAT_NV12,
        width=width,
        height=height,
        pts=pts,
    )


def make_packed_view(
    array: U8Plane,
    *,
    width: int,
    height: int,
    channels: int,
    pixel_format: int,
    pts: int,
    allow_copy: bool = False,
) -> native.FrameView:
    """Validate a packed image and return its borrowed native view."""
    planes = (np.asarray(array),)
    planes = _validate_planes(
        planes, ((height, width, channels),), label="packed", allow_copy=allow_copy
    )
    return _build_view(
        planes,
        pixel_format=pixel_format,
        width=width,
        height=height,
        pts=pts,
    )


def make_borrowed_view(
    frame: FrameInput,
    *,
    format: str,
    width: int,
    height: int,
    pts: int,
    allow_copy: bool = False,
) -> tuple[native.FrameView, tuple[U8Plane, ...]]:
    """Validate any supported borrowed format and retain its NumPy owners."""
    if format == "i420":
        if not isinstance(frame, tuple) or len(frame) != 3:
            raise ValueError("I420 borrowed input must contain (Y, U, V)")
        planes = tuple(np.asarray(plane) for plane in frame)
        expected = (
            (height, width),
            (height // 2, width // 2),
            (height // 2, width // 2),
        )
        pixel_format = native.MKVC_PIXEL_FORMAT_I420
    elif format == "nv12":
        if not isinstance(frame, tuple) or len(frame) != 2:
            raise ValueError("NV12 borrowed input must contain (Y, UV)")
        planes = tuple(np.asarray(plane) for plane in frame)
        expected = ((height, width), (height // 2, width))
        pixel_format = native.MKVC_PIXEL_FORMAT_NV12
    elif format in ("bgr", "rgb", "bgra"):
        if isinstance(frame, tuple):
            raise ValueError(f"{format} borrowed input must be one ndarray")
        channels, pixel_format = {
            "bgr": (3, native.MKVC_PIXEL_FORMAT_BGR24),
            "rgb": (3, native.MKVC_PIXEL_FORMAT_RGB24),
            "bgra": (4, native.MKVC_PIXEL_FORMAT_BGRA32),
        }[format]
        planes = (np.asarray(frame),)
        expected = ((height, width, channels),)
    else:
        raise ValueError("format must be i420, nv12, bgr, rgb, or bgra")
    original_planes = planes
    planes = _validate_planes(planes, expected, label="borrowed", allow_copy=allow_copy)
    view = _build_view(
        planes,
        pixel_format=pixel_format,
        width=width,
        height=height,
        pts=pts,
    )
    view._binding_normalized = any(
        normalized is not original for normalized, original in zip(planes, original_planes)
    )
    return view, planes
