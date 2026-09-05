"""CPU frame ownership, native pools, and asynchronous submission leases."""

from __future__ import annotations

import ctypes as ct

import numpy as np

from . import _native as native
from ._types import U8Plane


class _CpuFrameLease:
    """Pure-native decoded-frame owner retained by borrowed ndarray views."""
    def __init__(self, handle: native.FrameHandle) -> None:
        self._handle = handle

    def release(self) -> None:
        if self._handle:
            native.lib.mkvc_frame_release(self._handle)
            self._handle = native.FrameHandle()

    def __del__(self) -> None:
        self.release()


class _BorrowedArray(np.ndarray):
    """ndarray subclass that propagates the native frame lease to its views."""
    _mkvc_lease: object | None

    def __array_finalize__(self, source: object) -> None:
        self._mkvc_lease = getattr(source, "_mkvc_lease", None)


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
    def __init__(
        self, handle: native.FrameHandle, view: native.FrameView
    ) -> None:
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
                raw = np.ctypeslib.as_array(
                    view.planes[index], shape=(stride * height,)
                )
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
    def __enter__(self) -> "BorrowedCpuFrame": return self
    def __exit__(self, *args: object) -> None: self.close()
    def __del__(self) -> None: self.close()


class _CpuBufferLease:
    """Native pool-slot owner retained by every exported ndarray view."""
    def __init__(self, handle: native.CpuBufferHandle) -> None:
        self._handle = handle

    @property
    def handle(self) -> native.CpuBufferHandle:
        if not self._handle:
            raise RuntimeError("native CPU buffer is released")
        return self._handle

    def release(self) -> None:
        if self._handle:
            native.lib.mkvc_cpu_buffer_release(self._handle)
            self._handle = native.CpuBufferHandle()

    def __del__(self) -> None:
        self.release()


class CpuBuffer:
    """Expose writable NumPy views over a native CPU pool slot.

    A retained plane or slice keeps the slot leased even after this wrapper is
    closed. The slot is recycled only after the last Python/native owner exits.

    Attributes
    ----------
    planes : tuple of numpy.ndarray
        Writable plane views whose layout depends on ``format``.
    array : numpy.ndarray
        Packed image view. Access raises for planar formats.
    format : {"i420", "nv12", "bgr", "rgb", "bgra"}
        Pixel format of the allocation.
    width, height : int
        Frame dimensions in pixels.
    generation : int
        Pool slot generation used to reject stale native submissions.
    """
    def __init__(self, handle: native.CpuBufferHandle) -> None:
        lease = _CpuBufferLease(handle)
        desc = native.CpuBufferDesc()
        desc.struct_size, desc.struct_version = ct.sizeof(desc), 1
        view = native.MutableFrameView()
        view.struct_size, view.struct_version = ct.sizeof(view), 1
        try:
            native.check(native.lib.mkvc_cpu_buffer_get_desc(
                lease.handle, ct.byref(desc)
            ))
            native.check(native.lib.mkvc_cpu_buffer_get_view(
                lease.handle, ct.byref(view)
            ))
            layouts = self._layouts(desc)
            planes: list[_BorrowedArray] = []
            for index, (shape, column_bytes) in enumerate(layouts):
                stride = int(view.strides[index])
                rows = shape[0]
                if not view.planes[index] or stride < column_bytes:
                    raise RuntimeError("native CPU pool returned an invalid plane")
                raw = np.ctypeslib.as_array(
                    view.planes[index], shape=(stride * rows,)
                )
                if len(shape) == 2:
                    plane = raw.reshape(rows, stride)[:, :column_bytes]
                else:
                    plane = raw.reshape(rows, stride)[:, :column_bytes].reshape(shape)
                borrowed = plane.view(_BorrowedArray)
                borrowed._mkvc_lease = lease
                planes.append(borrowed)
        except Exception:
            lease.release()
            raise
        self._lease: _CpuBufferLease | None = lease
        self._planes: tuple[_BorrowedArray, ...] = tuple(planes)
        self.format = {
            native.MKVC_PIXEL_FORMAT_I420: "i420",
            native.MKVC_PIXEL_FORMAT_NV12: "nv12",
            native.MKVC_PIXEL_FORMAT_BGR24: "bgr",
            native.MKVC_PIXEL_FORMAT_RGB24: "rgb",
            native.MKVC_PIXEL_FORMAT_BGRA32: "bgra",
        }[desc.pixel_format]
        self.width = int(desc.width)
        self.height = int(desc.height)
        self.generation = int(desc.generation)

    @staticmethod
    def _layouts(desc: native.CpuBufferDesc) -> tuple[tuple[tuple[int, ...], int], ...]:
        width, height = int(desc.width), int(desc.height)
        if desc.pixel_format == native.MKVC_PIXEL_FORMAT_I420:
            return (((height, width), width),
                    ((height // 2, width // 2), width // 2),
                    ((height // 2, width // 2), width // 2))
        if desc.pixel_format == native.MKVC_PIXEL_FORMAT_NV12:
            return (((height, width), width), ((height // 2, width), width))
        channels = {
            native.MKVC_PIXEL_FORMAT_BGR24: 3,
            native.MKVC_PIXEL_FORMAT_RGB24: 3,
            native.MKVC_PIXEL_FORMAT_BGRA32: 4,
        }.get(desc.pixel_format)
        if channels is None:
            raise RuntimeError("native CPU pool returned an unsupported format")
        return (((height, width, channels), width * channels),)

    @property
    def planes(self) -> tuple[U8Plane, ...]:
        """tuple of numpy.ndarray: Writable views of the buffer planes."""
        if self._lease is None:
            raise RuntimeError("native CPU buffer is closed")
        return self._planes

    @property
    def array(self) -> U8Plane:
        """numpy.ndarray: Writable packed image view.

        Raises
        ------
        RuntimeError
            If the buffer uses a planar format.
        """
        if len(self.planes) != 1:
            raise RuntimeError("planar CPU buffers do not have a single array")
        return self.planes[0]

    def _native_handle(self) -> native.CpuBufferHandle:
        if self._lease is None:
            raise RuntimeError("native CPU buffer is closed")
        return self._lease.handle

    def close(self) -> None:
        """Release this wrapper's ownership of the pool slot."""
        if getattr(self, "_lease", None) is not None:
            self._planes = ()
            self._lease = None

    release = close
    def __enter__(self) -> "CpuBuffer": return self
    def __exit__(self, *args: object) -> None: self.close()
    def __del__(self) -> None: self.close()


class CpuFramePool:
    """Manage a fixed-capacity reusable native CPU input buffer pool.

    Parameters
    ----------
    format : {"i420", "nv12", "bgr", "rgb", "bgra"}
        Pixel format allocated for every slot.
    frame_size : tuple of int
        Even ``(width, height)`` dimensions in pixels.
    capacity : int
        Maximum number of simultaneously leased slots.
    """
    def __init__(self, format: str, frame_size: tuple[int, int], capacity: int) -> None:
        formats = {
            "i420": native.MKVC_PIXEL_FORMAT_I420,
            "nv12": native.MKVC_PIXEL_FORMAT_NV12,
            "bgr": native.MKVC_PIXEL_FORMAT_BGR24,
            "rgb": native.MKVC_PIXEL_FORMAT_RGB24,
            "bgra": native.MKVC_PIXEL_FORMAT_BGRA32,
        }
        if format not in formats:
            raise ValueError("format must be i420, nv12, bgr, rgb, or bgra")
        width, height = frame_size
        if (not isinstance(width, int) or not isinstance(height, int) or
                width <= 0 or height <= 0 or width & 1 or height & 1 or
                width > 0xFFFFFFFF or height > 0xFFFFFFFF):
            raise ValueError("frame_size must contain positive even uint32 values")
        if (not isinstance(capacity, int) or capacity <= 0 or
                capacity > 0xFFFFFFFF):
            raise ValueError("capacity must be a positive uint32 value")
        config = native.CpuFramePoolConfig()
        config.struct_size, config.struct_version = ct.sizeof(config), 1
        config.pixel_format = formats[format]
        config.width, config.height, config.capacity = width, height, capacity
        self._handle = native.CpuFramePoolHandle()
        native.check(native.lib.mkvc_cpu_frame_pool_create(
            ct.byref(config), ct.byref(self._handle)
        ))
        self.format = format
        self.frame_size = frame_size
        self.capacity = capacity

    def acquire(self, timeout_ms: int = 0xFFFFFFFF) -> CpuBuffer:
        """Acquire a writable buffer, waiting for capacity when necessary.

        Parameters
        ----------
        timeout_ms : int, default: 4294967295
            Maximum wait in milliseconds. The default waits indefinitely.

        Returns
        -------
        CpuBuffer
            A leased writable pool slot.

        Raises
        ------
        RuntimeError
            If the pool is closed, the wait times out, or acquisition fails.
        ValueError
            If ``timeout_ms`` is outside the uint32 range.
        """
        if not self._handle:
            raise RuntimeError("native CPU frame pool is closed")
        if timeout_ms < 0 or timeout_ms > 0xFFFFFFFF:
            raise ValueError("timeout_ms is outside uint32 range")
        handle = native.CpuBufferHandle()
        native.check(native.lib.mkvc_cpu_frame_pool_acquire(
            self._handle, timeout_ms, ct.byref(handle)
        ))
        return CpuBuffer(handle)

    def try_acquire(self) -> CpuBuffer | None:
        """Try to acquire a buffer without blocking.

        Returns
        -------
        CpuBuffer or None
            A leased slot, or ``None`` when the pool is at capacity.
        """
        if not self._handle:
            raise RuntimeError("native CPU frame pool is closed")
        handle = native.CpuBufferHandle()
        result = native.lib.mkvc_cpu_frame_pool_acquire(
            self._handle, 0, ct.byref(handle)
        )
        if result == native.MKVC_WOULD_BLOCK:
            return None
        native.check(result)
        return CpuBuffer(handle)

    def close(self) -> None:
        """Close the pool after outstanding slots finish their leases."""
        if getattr(self, "_handle", None):
            native.lib.mkvc_cpu_frame_pool_destroy(self._handle)
            self._handle = native.CpuFramePoolHandle()

    release = close
    def __enter__(self) -> "CpuFramePool": return self
    def __exit__(self, *args: object) -> None: self.close()
    def __del__(self) -> None: self.close()


class Submission:
    """Retain asynchronously borrowed Python input until native completion.

    Attributes
    ----------
    done : bool
        Whether native processing has completed.
    """
    def __init__(self, handle: native.SubmissionHandle, owner: object) -> None:
        self._handle = handle
        self._owner: object | None = owner

    @property
    def done(self) -> bool:
        """bool: Whether native processing has completed."""
        if not self._handle:
            return True
        status = ct.c_uint32()
        native.check(native.lib.mkvc_submission_query(
            self._handle, ct.byref(status)
        ))
        if status.value != native.MKVC_SUBMISSION_PENDING:
            self._owner = None
            return True
        return False

    def wait(self, timeout_ms: int = 0xFFFFFFFF) -> None:
        """Wait for native processing to complete.

        Parameters
        ----------
        timeout_ms : int, default: 4294967295
            Maximum wait in milliseconds. The default waits indefinitely.

        Raises
        ------
        RuntimeError
            If native processing fails or the wait times out.
        ValueError
            If ``timeout_ms`` is outside the uint32 range.
        """
        if not self._handle:
            return
        if timeout_ms < 0 or timeout_ms > 0xFFFFFFFF:
            raise ValueError("timeout_ms is outside uint32 range")
        result = native.lib.mkvc_submission_wait(self._handle, timeout_ms)
        if result != native.MKVC_ERROR_TIMEOUT:
            self._owner = None
        native.check(result)

    def close(self) -> None:
        """Release the completion handle and retained input owner."""
        if self._handle:
            native.lib.mkvc_submission_release(self._handle)
            self._handle = native.SubmissionHandle()
            self._owner = None

    release = close
    def __enter__(self) -> "Submission": return self
    def __exit__(self, *args: object) -> None: self.close()
    def __del__(self) -> None:
        if getattr(self, "_handle", None):
            self.close()
