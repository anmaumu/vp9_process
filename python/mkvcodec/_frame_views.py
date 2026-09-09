"""Validate NumPy frame layouts and build native CPU frame views."""

from __future__ import annotations

import ctypes as ct

import numpy as np

from . import _native as native
from ._io_common import _plane_pointer
from ._types import U8Plane


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
    return view


def _validate_planes(
    planes: tuple[U8Plane, ...],
    expected: tuple[tuple[int, ...], ...],
    *,
    label: str,
) -> None:
    """Reject layouts that the native row-stride ABI cannot represent."""
    for plane, shape in zip(planes, expected):
        if plane.dtype != np.uint8 or plane.shape != shape:
            raise ValueError(f"{label} plane must be uint8 with shape {shape}")
        if plane.strides[0] <= 0 or plane.strides[-1] != 1:
            raise ValueError(f"{label} planes require positive packed element stride")
        if plane.ndim == 3 and plane.strides[1] != plane.shape[2]:
            raise ValueError(f"{label} packed frame must have interleaved channels")


def make_i420_view(
    y: U8Plane,
    u: U8Plane,
    v: U8Plane,
    *,
    width: int,
    height: int,
    pts: int,
) -> native.FrameView:
    """Validate an I420 plane tuple and return its borrowed native view."""
    planes = tuple(np.asarray(plane) for plane in (y, u, v))
    expected = (
        (height, width),
        (height // 2, width // 2),
        (height // 2, width // 2),
    )
    _validate_planes(planes, expected, label="I420")
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
) -> native.FrameView:
    """Validate an NV12 plane tuple and return its borrowed native view."""
    planes = tuple(np.asarray(plane) for plane in (y, uv))
    expected = ((height, width), (height // 2, width))
    _validate_planes(planes, expected, label="NV12")
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
) -> native.FrameView:
    """Validate a packed image and return its borrowed native view."""
    planes = (np.asarray(array),)
    _validate_planes(planes, ((height, width, channels),), label="packed")
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
    _validate_planes(planes, expected, label="borrowed")
    return (
        _build_view(
            planes,
            pixel_format=pixel_format,
            width=width,
            height=height,
            pts=pts,
        ),
        planes,
    )
