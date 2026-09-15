"""Copy native decoded frames into owned NumPy output objects."""

from __future__ import annotations

import ctypes as ct

import numpy as np

from ..native import library as native
from .io_common import _plane_pointer
from ..api.frame import CpuFrame, U8Plane


def get_frame_view(handle: native.FrameHandle) -> native.FrameView:
    """Return the versioned immutable view for a native frame handle."""
    view = native.FrameView()
    view.struct_size = ct.sizeof(view)
    view.struct_version = 1
    native.check(native.lib.mkvc_frame_get_view(handle, ct.byref(view)))
    return view


def copy_i420(handle: native.FrameHandle, view: native.FrameView) -> CpuFrame:
    """Copy an I420 frame through the native path so the edge is observable."""
    y = np.empty((view.height, view.width), dtype=np.uint8)
    u = np.empty((view.height // 2, view.width // 2), dtype=np.uint8)
    v = np.empty((view.height // 2, view.width // 2), dtype=np.uint8)
    destination = native.MutableFrameView()
    destination.struct_size = ct.sizeof(destination)
    destination.struct_version = 1
    destination.pixel_format = native.MKVC_PIXEL_FORMAT_I420
    destination.width = view.width
    destination.height = view.height
    for index, plane in enumerate((y, u, v)):
        destination.planes[index] = _plane_pointer(plane)
        destination.strides[index] = plane.strides[0]
    native.check(native.lib.mkvc_frame_copy_to(handle, ct.byref(destination)))
    return CpuFrame(y, u, v, int(destination.pts))


def copy_nv12(
    handle: native.FrameHandle, view: native.FrameView
) -> tuple[tuple[U8Plane, U8Plane], int]:
    """Copy a native frame into owned NV12 planes and return its timestamp."""
    y = np.empty((view.height, view.width), dtype=np.uint8)
    uv = np.empty((view.height // 2, view.width), dtype=np.uint8)
    destination = native.MutableFrameView()
    destination.struct_size = ct.sizeof(destination)
    destination.struct_version = 1
    destination.pixel_format = native.MKVC_PIXEL_FORMAT_NV12
    destination.width = view.width
    destination.height = view.height
    destination.planes[0] = _plane_pointer(y)
    destination.planes[1] = _plane_pointer(uv)
    destination.strides[0] = y.strides[0]
    destination.strides[1] = uv.strides[0]
    native.check(native.lib.mkvc_frame_copy_to(handle, ct.byref(destination)))
    return (y, uv), int(destination.pts)


def copy_packed(
    handle: native.FrameHandle,
    view: native.FrameView,
    *,
    channels: int,
    pixel_format: int,
    conversion_threads: int,
) -> tuple[U8Plane, int]:
    """Copy a native frame into an owned packed-color array and timestamp."""
    output = np.empty((view.height, view.width, channels), dtype=np.uint8)
    destination = native.MutableFrameView()
    destination.struct_size = ct.sizeof(destination)
    destination.struct_version = 1
    destination.pixel_format = pixel_format
    destination.width = view.width
    destination.height = view.height
    destination.planes[0] = _plane_pointer(output)
    destination.strides[0] = output.strides[0]
    options = native.FrameCopyOptions()
    options.struct_size = ct.sizeof(options)
    options.struct_version = 1
    options.conversion_threads = conversion_threads
    native.check(native.lib.mkvc_frame_copy_to_ex(handle, ct.byref(destination), ct.byref(options)))
    return output, int(destination.pts)
