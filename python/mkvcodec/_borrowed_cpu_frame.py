"""Read-only NumPy views over native decoded CPU frames."""

from __future__ import annotations

import numpy as np

from . import _native as native
from ._cpu_array import _BorrowedArray
from ._types import U8Plane


class _CpuFrameLease:
    """Own a decoded native frame retained by borrowed NumPy views."""

    def __init__(self, handle: native.FrameHandle) -> None:
        self._handle = handle

    def release(self) -> None:
        """Release the native frame once."""
        if self._handle:
            native.lib.mkvc_frame_release(self._handle)
            self._handle = native.FrameHandle()

    def __del__(self) -> None:
        self.release()


class BorrowedCpuFrame:
    """Expose read-only NumPy views over a native decoded I420 frame.

    Closing this wrapper drops its references. Any plane or slice retained by
    the caller keeps the native frame alive until that last ndarray is released.

    Attributes
    ----------
    y, u, v : numpy.ndarray
        Read-only two-dimensional uint8 views of the I420 planes.
    planes : tuple of numpy.ndarray
        The ``(y, u, v)`` plane views.
    pts_ns : int
        Presentation timestamp in nanoseconds.
    width, height : int
        Visible frame dimensions in pixels.
    pixel_format : str
        Always ``"i420"``.
    """

    def __init__(self, handle: native.FrameHandle, view: native.FrameView) -> None:
        if view.pixel_format != native.MKVC_PIXEL_FORMAT_I420:
            native.lib.mkvc_frame_release(handle)
            raise RuntimeError("native decoder returned a non-I420 frame")
        lease = _CpuFrameLease(handle)
        planes: list[_BorrowedArray] = []
        try:
            for index in range(3):
                width = view.width if index == 0 else view.width // 2
                height = view.height if index == 0 else view.height // 2
                stride = int(view.strides[index])
                if not view.planes[index] or stride < width:
                    raise RuntimeError("native decoder returned an invalid I420 plane")
                raw = np.ctypeslib.as_array(view.planes[index], shape=(stride * height,))
                plane = raw.reshape(height, stride)[:, :width].view(_BorrowedArray)
                plane._mkvc_lease = lease
                plane.flags.writeable = False
                planes.append(plane)
        except Exception:
            lease.release()
            raise
        self._lease: _CpuFrameLease | None = lease
        self._planes: tuple[_BorrowedArray, ...] = tuple(planes)
        self.pts_ns = int(view.pts)
        self.width = int(view.width)
        self.height = int(view.height)
        self.pixel_format = "i420"

    def _plane(self, index: int) -> U8Plane:
        if self._lease is None:
            raise RuntimeError("borrowed CPU frame is closed")
        return self._planes[index]

    @property
    def y(self) -> U8Plane:
        """numpy.ndarray: Read-only luma plane view."""
        return self._plane(0)

    @property
    def u(self) -> U8Plane:
        """numpy.ndarray: Read-only U chroma plane view."""
        return self._plane(1)

    @property
    def v(self) -> U8Plane:
        """numpy.ndarray: Read-only V chroma plane view."""
        return self._plane(2)

    @property
    def planes(self) -> tuple[U8Plane, U8Plane, U8Plane]:
        """tuple of numpy.ndarray: Read-only ``(y, u, v)`` views."""
        return self.y, self.u, self.v

    def close(self) -> None:
        """Release this wrapper's ownership of the native frame.

        NumPy views already retained by the caller remain valid and keep the
        native allocation leased until their last derived view is released.
        """
        if getattr(self, "_lease", None) is not None:
            self._planes = ()
            self._lease = None

    release = close

    def __enter__(self) -> "BorrowedCpuFrame":
        return self

    def __exit__(self, *args: object) -> None:
        self.close()

    def __del__(self) -> None:
        self.close()
