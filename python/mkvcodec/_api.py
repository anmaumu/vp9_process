from __future__ import annotations

import ctypes as ct
from dataclasses import dataclass
from fractions import Fraction
from pathlib import Path
from typing import Iterator

import numpy as np
import numpy.typing as npt

from . import _native as native

try:
    from . import _dlpack
except ImportError:
    _dlpack = None

U8Plane = npt.NDArray[np.uint8]


@dataclass(frozen=True)
class CpuFrame:
    y: U8Plane
    u: U8Plane
    v: U8Plane
    pts_ns: int


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
    """Read-only zero-copy views over one native decoded I420 frame.

    Closing this wrapper drops its references. Any plane or slice retained by
    the caller keeps the native frame alive until that last ndarray is released.
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
    def y(self) -> U8Plane: return self._plane(0)
    @property
    def u(self) -> U8Plane: return self._plane(1)
    @property
    def v(self) -> U8Plane: return self._plane(2)
    @property
    def planes(self) -> tuple[U8Plane, U8Plane, U8Plane]:
        return self.y, self.u, self.v

    def close(self) -> None:
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
    """Writable NumPy views over one generation-checked native pool slot.

    A retained plane or slice keeps the slot leased even after this wrapper is
    closed. The slot is recycled only after the last Python/native owner exits.
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
        if self._lease is None:
            raise RuntimeError("native CPU buffer is closed")
        return self._planes

    @property
    def array(self) -> U8Plane:
        if len(self.planes) != 1:
            raise RuntimeError("planar CPU buffers do not have a single array")
        return self.planes[0]

    def _native_handle(self) -> native.CpuBufferHandle:
        if self._lease is None:
            raise RuntimeError("native CPU buffer is closed")
        return self._lease.handle

    def close(self) -> None:
        if getattr(self, "_lease", None) is not None:
            self._planes = ()
            self._lease = None

    release = close
    def __enter__(self) -> "CpuBuffer": return self
    def __exit__(self, *args: object) -> None: self.close()
    def __del__(self) -> None: self.close()


class CpuFramePool:
    """Fixed-capacity reusable native CPU input buffer pool."""
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
        if getattr(self, "_handle", None):
            native.lib.mkvc_cpu_frame_pool_destroy(self._handle)
            self._handle = native.CpuFramePoolHandle()

    release = close
    def __enter__(self) -> "CpuFramePool": return self
    def __exit__(self, *args: object) -> None: self.close()
    def __del__(self) -> None: self.close()


@dataclass(frozen=True)
class PipelineMetrics:
    accepted_frames: int
    completed_frames: int
    rejected_frames: int
    queue_wait_ns: int
    backend_time_ns: int
    queue_capacity: int
    peak_queue_depth: int
    hardware_pending_peak: int
    copy_path: str


class Submission:
    """Completion lease retaining asynchronously borrowed Python input."""
    def __init__(self, handle: native.SubmissionHandle, owner: object) -> None:
        self._handle = handle
        self._owner: object | None = owner

    @property
    def done(self) -> bool:
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
        if not self._handle:
            return
        if timeout_ms < 0 or timeout_ms > 0xFFFFFFFF:
            raise ValueError("timeout_ms is outside uint32 range")
        result = native.lib.mkvc_submission_wait(self._handle, timeout_ms)
        if result != native.MKVC_ERROR_TIMEOUT:
            self._owner = None
        native.check(result)

    def close(self) -> None:
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


class GpuFrame:
    """Lease over a backend-owned GPU frame and its borrowed native handle."""
    def __init__(self, handle: native.GpuFrameHandle) -> None:
        self._handle = handle
        self._closed = False

    @property
    def descriptor(self) -> dict[str, object]:
        if self._closed:
            raise RuntimeError("GPU frame is released")
        value = native.GpuFrameDesc()
        value.struct_size, value.struct_version = ct.sizeof(value), 1
        native.check(native.lib.mkvc_gpu_frame_get_desc(self._handle, ct.byref(value)))
        return {
            "backend": value.backend, "memory_type": value.memory_type,
            "device_id": value.device_id, "generation": value.generation,
            "pixel_format": value.pixel_format, "width": value.width,
            "height": value.height, "plane_count": value.plane_count,
            "plane_offsets": tuple(value.plane_offsets),
            "pitches": tuple(value.pitches), "pts_ns": value.pts,
        }

    @property
    def native_handle(self) -> dict[str, object]:
        if self._closed:
            raise RuntimeError("GPU frame is released")
        value = native.GpuNativeHandleDesc()
        value.struct_size, value.struct_version = ct.sizeof(value), 1
        native.check(native.lib.mkvc_gpu_frame_get_native_handle(
            self._handle, ct.byref(value)
        ))
        return {
            "type": value.type, "borrowed": bool(value.borrowed),
            "device_id": value.device_id, "generation": value.generation,
            "handles": tuple(value.handles),
        }

    def wait(self, timeout_ms: int = 0xFFFFFFFF) -> None:
        if self._closed:
            raise RuntimeError("GPU frame is released")
        if timeout_ms < 0 or timeout_ms > 0xFFFFFFFF:
            raise ValueError("timeout_ms is outside uint32 range")
        native.check(native.lib.mkvc_gpu_frame_wait(self._handle, timeout_ms))

    def plane(self, index: int) -> "GpuPlane":
        """Return one GPU plane implementing the Python DLPack protocol."""
        if self._closed:
            raise RuntimeError("GPU frame is released")
        descriptor = self.descriptor
        if index < 0 or index >= int(descriptor["plane_count"]):
            raise IndexError("GPU plane index is out of range")
        return GpuPlane(self, index)

    def close(self) -> None:
        if not self._closed:
            native.lib.mkvc_gpu_frame_release(self._handle)
            self._handle = native.GpuFrameHandle()
            self._closed = True

    release = close
    def __enter__(self) -> "GpuFrame": return self
    def __exit__(self, *args: object) -> None: self.close()
    def __del__(self) -> None:
        if getattr(self, "_closed", True) is False:
            self.close()


class GpuPlane:
    """A zero-copy uint8 view of one linear GPU plane."""
    def __init__(self, frame: GpuFrame, index: int) -> None:
        self._frame = frame
        self._index = index

    def __dlpack_device__(self) -> tuple[int, int]:
        descriptor = self._frame.descriptor
        if descriptor["memory_type"] != 3:
            raise BufferError("this GPU surface is not a linear CUDA pointer")
        return 2, int(descriptor["device_id"])

    def __dlpack__(
        self, *, stream: int | None = None, max_version: tuple[int, int] | None = None,
        dl_device: tuple[int, int] | None = None, copy: bool | None = None,
    ) -> object:
        del max_version
        if copy:
            raise BufferError("mkvcodec DLPack planes do not implement copies")
        device = self.__dlpack_device__()
        if dl_device is not None and tuple(dl_device) != device:
            raise BufferError("requested DLPack device does not match the GPU frame")
        if _dlpack is None:
            raise RuntimeError(
                "the mkvcodec stable-ABI DLPack capsule extension is not installed"
            )
        if stream is None:
            consumer_stream = 0
        elif not isinstance(stream, int) or stream < 0 or stream > 0xFFFFFFFFFFFFFFFF:
            raise ValueError("DLPack stream must be a uint64 integer or None")
        else:
            consumer_stream = stream
        managed = ct.c_void_p()
        native.check(native.lib.mkvc_gpu_frame_export_dlpack(
            self._frame._handle, self._index, consumer_stream, ct.byref(managed)
        ))
        try:
            return _dlpack.capsule_from_address(managed.value)
        except Exception:
            # Capsule construction is the only operation after native ownership
            # transfer. Reclaim through the standard managed-tensor deleter.
            native.lib.mkvc_dlpack_managed_tensor_release(managed)
            raise


def _read_metrics(handle: ct.c_void_p, function: object) -> PipelineMetrics:
    metrics = native.PipelineMetrics()
    metrics.struct_size = ct.sizeof(metrics)
    metrics.struct_version = 1
    native.check(function(handle, ct.byref(metrics)))
    paths = {0: "unknown", 1: "cpu", 2: "zero_copy", 3: "mixed"}
    return PipelineMetrics(
        accepted_frames=metrics.accepted_frames,
        completed_frames=metrics.completed_frames,
        rejected_frames=metrics.rejected_frames,
        queue_wait_ns=metrics.queue_wait_ns,
        backend_time_ns=metrics.backend_time_ns,
        queue_capacity=metrics.queue_capacity,
        peak_queue_depth=metrics.peak_queue_depth,
        hardware_pending_peak=metrics.hardware_pending_peak,
        copy_path=paths.get(metrics.copy_path, f"unknown_{metrics.copy_path}"),
    )


def _fps_fraction(fps: float | int | tuple[int, int]) -> Fraction:
    if isinstance(fps, tuple):
        value = Fraction(*fps)
    else:
        value = Fraction(fps).limit_denominator(100_000)
    if value <= 0:
        raise ValueError("fps must be positive")
    return value


def _plane_pointer(array: U8Plane) -> ct.POINTER(ct.c_uint8):
    return ct.cast(int(array.ctypes.data), ct.POINTER(ct.c_uint8))


class VideoWriter:
    def __init__(
        self,
        path: str | Path,
        *,
        codec: str = "vp9",
        backend: str = "cpu",
        fps: float | int | tuple[int, int],
        frame_size: tuple[int, int],
        quality: int = 32,
        keyframe_interval_frames: int = 0,
        threads: int = 0,
        queue_size: int = 8,
        require_gpu_resident: bool = False,
    ) -> None:
        if codec not in ("vp9", "av1") or backend not in (
            "cpu", "intel", "nvidia"
        ):
            raise ValueError(
                "the Python writer supports VP9/AV1 on CPU, Intel or NVIDIA"
            )
        if require_gpu_resident and backend not in ("intel", "nvidia"):
            raise ValueError(
                "require_gpu_resident requires the Intel or NVIDIA backend"
            )
        if require_gpu_resident and queue_size != 0:
            raise ValueError(
                "require_gpu_resident currently requires queue_size=0"
            )
        width, height = frame_size
        rate = _fps_fraction(fps)
        encoded_path = str(Path(path)).encode("utf-8")
        config = native.EncoderConfig()
        config.struct_size = ct.sizeof(config)
        config.struct_version = 1
        config.output_path_utf8 = encoded_path
        config.codec = (native.MKVC_CODEC_VP9 if codec == "vp9" else
                        native.MKVC_CODEC_AV1)
        config.backend = {
            "cpu": native.MKVC_BACKEND_CPU,
            "intel": native.MKVC_BACKEND_INTEL,
            "nvidia": native.MKVC_BACKEND_NVIDIA,
        }[backend]
        config.width = width
        config.height = height
        config.fps_num = rate.numerator
        config.fps_den = rate.denominator
        config.quality = quality
        config.keyframe_interval_frames = keyframe_interval_frames
        config.threads = threads
        if queue_size < 0:
            raise ValueError("queue_size must be zero or positive")
        config.queue_size = queue_size
        self._handle = native.EncoderHandle()
        native.check(native.lib.mkvc_encoder_create(ct.byref(config), ct.byref(self._handle)))
        if require_gpu_resident:
            policy = native.CopyPolicy()
            policy.struct_size = ct.sizeof(policy)
            policy.struct_version = 1
            policy.require_gpu_resident = 1
            policy.allow_gpu_copy = 1
            policy.allow_cpu_copy = 0
            result = native.lib.mkvc_encoder_set_copy_policy(
                self._handle, ct.byref(policy)
            )
            if result != native.MKVC_OK:
                native.lib.mkvc_encoder_destroy(self._handle)
                self._handle = native.EncoderHandle()
                native.check(result)
        self._width = width
        self._height = height
        self._closed = False
        self._require_gpu_resident = bool(require_gpu_resident)
        self._last_metrics: PipelineMetrics | None = None

    @property
    def metrics(self) -> PipelineMetrics:
        if self._closed:
            if self._last_metrics is None:
                raise RuntimeError("writer metrics are unavailable")
            return self._last_metrics
        return _read_metrics(self._handle, native.lib.mkvc_encoder_get_metrics)

    def _submit(self, frame: native.FrameView, *, block: bool) -> bool:
        if self._require_gpu_resident:
            raise RuntimeError(
                "CPU frame submission is disabled by require_gpu_resident=True"
            )
        function = (native.lib.mkvc_encoder_write_frame if block else
                    native.lib.mkvc_encoder_try_write_frame)
        result = function(self._handle, ct.byref(frame))
        if result == native.MKVC_WOULD_BLOCK:
            return False
        native.check(result)
        return True

    def _submit_borrowed(self, frame: native.FrameView) -> None:
        if self._require_gpu_resident:
            raise RuntimeError(
                "CPU frame submission is disabled by require_gpu_resident=True"
            )
        native.check(native.lib.mkvc_encoder_write_frame_borrowed(
            self._handle, ct.byref(frame)
        ))

    def _write_i420(
        self, y: U8Plane, u: U8Plane, v: U8Plane, *, pts: int, block: bool
    ) -> bool:
        if self._closed:
            raise RuntimeError("writer is closed")
        planes = tuple(np.asarray(plane) for plane in (y, u, v))
        expected = (
            (self._height, self._width),
            (self._height // 2, self._width // 2),
            (self._height // 2, self._width // 2),
        )
        for plane, shape in zip(planes, expected):
            if plane.dtype != np.uint8 or plane.ndim != 2 or plane.shape != shape:
                raise ValueError(f"I420 plane must be uint8 with shape {shape}")
            if plane.strides[0] <= 0 or plane.strides[1] != 1:
                raise ValueError("I420 planes require positive row stride and packed columns")
        frame = native.FrameView()
        frame.struct_size = ct.sizeof(frame)
        frame.struct_version = 1
        frame.pixel_format = native.MKVC_PIXEL_FORMAT_I420
        frame.width = self._width
        frame.height = self._height
        for index, plane in enumerate(planes):
            frame.planes[index] = _plane_pointer(plane)
            frame.strides[index] = plane.strides[0]
        frame.pts = pts
        return self._submit(frame, block=block)

    def write_i420(self, y: U8Plane, u: U8Plane, v: U8Plane, *, pts: int = -1) -> None:
        self._write_i420(y, u, v, pts=pts, block=True)

    def _write_nv12(self, y: U8Plane, uv: U8Plane, *, pts: int, block: bool) -> bool:
        if self._closed:
            raise RuntimeError("writer is closed")
        planes = (np.asarray(y), np.asarray(uv))
        expected = ((self._height, self._width),
                    (self._height // 2, self._width))
        for plane, shape in zip(planes, expected):
            if plane.dtype != np.uint8 or plane.ndim != 2 or plane.shape != shape:
                raise ValueError(f"NV12 plane must be uint8 with shape {shape}")
            if plane.strides[0] <= 0 or plane.strides[1] != 1:
                raise ValueError("NV12 planes require positive row stride and packed columns")
        frame = native.FrameView()
        frame.struct_size = ct.sizeof(frame)
        frame.struct_version = 1
        frame.pixel_format = native.MKVC_PIXEL_FORMAT_NV12
        frame.width = self._width
        frame.height = self._height
        for index, plane in enumerate(planes):
            frame.planes[index] = _plane_pointer(plane)
            frame.strides[index] = plane.strides[0]
        frame.pts = pts
        return self._submit(frame, block=block)

    def write_nv12(self, y: U8Plane, uv: U8Plane, *, pts: int = -1) -> None:
        self._write_nv12(y, uv, pts=pts, block=True)

    def write_surface(self, frame: GpuFrame) -> None:
        """Submit a compatible GPU frame without a CPU pixel copy."""
        if self._closed:
            raise RuntimeError("writer is closed")
        if not isinstance(frame, GpuFrame) or not frame._handle:
            raise ValueError("frame must be an open GpuFrame")
        native.check(native.lib.mkvc_encoder_write_gpu_frame(
            self._handle, frame._handle
        ))

    def _write_packed(
        self, array: U8Plane, channels: int, pixel_format: int, *, pts: int,
        block: bool = True,
    ) -> bool:
        if self._closed:
            raise RuntimeError("writer is closed")
        packed = np.asarray(array)
        expected = (self._height, self._width, channels)
        if packed.dtype != np.uint8 or packed.ndim != 3 or packed.shape != expected:
            raise ValueError(f"packed frame must be uint8 with shape {expected}")
        if packed.strides[0] <= 0 or packed.strides[1] != channels or packed.strides[2] != 1:
            raise ValueError("packed frame requires interleaved channels and positive row stride")
        frame = native.FrameView()
        frame.struct_size = ct.sizeof(frame)
        frame.struct_version = 1
        frame.pixel_format = pixel_format
        frame.width = self._width
        frame.height = self._height
        frame.planes[0] = _plane_pointer(packed)
        frame.strides[0] = packed.strides[0]
        frame.pts = pts
        return self._submit(frame, block=block)

    def write_bgr(self, frame: U8Plane, *, pts: int = -1) -> None:
        self._write_packed(frame, 3, native.MKVC_PIXEL_FORMAT_BGR24, pts=pts)

    def write_rgb(self, frame: U8Plane, *, pts: int = -1) -> None:
        self._write_packed(frame, 3, native.MKVC_PIXEL_FORMAT_RGB24, pts=pts)

    def write_bgra(self, frame: U8Plane, *, pts: int = -1) -> None:
        self._write_packed(frame, 4, native.MKVC_PIXEL_FORMAT_BGRA32, pts=pts)

    def write(
        self, frame: U8Plane | tuple[U8Plane, U8Plane, U8Plane], *, pts: int = -1
    ) -> None:
        if isinstance(frame, tuple):
            if len(frame) != 3:
                raise ValueError("I420 tuple must contain (Y, U, V)")
            self.write_i420(*frame, pts=pts)
            return
        self.write_bgr(frame, pts=pts)

    def try_write(
        self, frame: U8Plane | tuple[U8Plane, U8Plane, U8Plane], *, pts: int = -1
    ) -> bool:
        """Submit without waiting; return False when the bounded queue is full."""
        if isinstance(frame, tuple):
            if len(frame) != 3:
                raise ValueError("I420 tuple must contain (Y, U, V)")
            return self._write_i420(*frame, pts=pts, block=False)
        return self._write_packed(
            frame, 3, native.MKVC_PIXEL_FORMAT_BGR24, pts=pts, block=False
        )

    def _make_borrowed_view(
        self,
        frame: U8Plane | tuple[U8Plane, U8Plane] |
               tuple[U8Plane, U8Plane, U8Plane],
        *,
        format: str = "bgr",
        pts: int = -1,
    ) -> tuple[native.FrameView, tuple[U8Plane, ...]]:
        if self._closed:
            raise RuntimeError("writer is closed")
        if format == "i420":
            if not isinstance(frame, tuple) or len(frame) != 3:
                raise ValueError("I420 borrowed input must contain (Y, U, V)")
            planes = tuple(np.asarray(plane) for plane in frame)
            expected = (
                (self._height, self._width),
                (self._height // 2, self._width // 2),
                (self._height // 2, self._width // 2),
            )
            pixel_format = native.MKVC_PIXEL_FORMAT_I420
        elif format == "nv12":
            if not isinstance(frame, tuple) or len(frame) != 2:
                raise ValueError("NV12 borrowed input must contain (Y, UV)")
            planes = tuple(np.asarray(plane) for plane in frame)
            expected = (
                (self._height, self._width),
                (self._height // 2, self._width),
            )
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
            expected = ((self._height, self._width, channels),)
        else:
            raise ValueError("format must be i420, nv12, bgr, rgb, or bgra")
        for plane, shape in zip(planes, expected):
            if plane.dtype != np.uint8 or plane.shape != shape:
                raise ValueError(f"borrowed plane must be uint8 with shape {shape}")
            if plane.strides[0] <= 0 or plane.strides[-1] != 1:
                raise ValueError("borrowed planes require positive packed element stride")
            if plane.ndim == 3 and plane.strides[1] != plane.shape[2]:
                raise ValueError("borrowed packed frame must have interleaved channels")
        view = native.FrameView()
        view.struct_size, view.struct_version = ct.sizeof(view), 1
        view.pixel_format = pixel_format
        view.width, view.height, view.pts = self._width, self._height, pts
        for index, plane in enumerate(planes):
            view.planes[index] = _plane_pointer(plane)
            view.strides[index] = plane.strides[0]
        return view, planes

    def write_borrowed(
        self,
        frame: U8Plane | tuple[U8Plane, U8Plane] |
               tuple[U8Plane, U8Plane, U8Plane],
        *,
        format: str = "bgr",
        pts: int = -1,
    ) -> None:
        """Synchronously borrow CPU memory until the codec has read the frame.

        The initial implementation requires ``queue_size=0``. No copy is made
        at the C ABI boundary; codec-required color conversion may still copy.
        """
        view, _ = self._make_borrowed_view(frame, format=format, pts=pts)
        self._submit_borrowed(view)

    def submit_borrowed(
        self,
        frame: U8Plane | tuple[U8Plane, U8Plane] |
               tuple[U8Plane, U8Plane, U8Plane],
        *,
        format: str = "bgr",
        pts: int = -1,
    ) -> Submission:
        """Queue borrowed input and retain its owner until completion."""
        if self._require_gpu_resident:
            raise RuntimeError(
                "CPU frame submission is disabled by require_gpu_resident=True"
            )
        view, owner = self._make_borrowed_view(frame, format=format, pts=pts)
        handle = native.SubmissionHandle()
        native.check(native.lib.mkvc_encoder_submit_frame_borrowed(
            self._handle, ct.byref(view), ct.byref(handle)
        ))
        return Submission(handle, owner)

    def submit_buffer(self, buffer: CpuBuffer, *, pts: int = -1) -> Submission:
        """Queue a native pool buffer and retain its slot through completion."""
        if self._closed:
            raise RuntimeError("writer is closed")
        if self._require_gpu_resident:
            raise RuntimeError(
                "CPU frame submission is disabled by require_gpu_resident=True"
            )
        if not isinstance(buffer, CpuBuffer):
            raise ValueError("buffer must be an open CpuBuffer")
        if (buffer.width, buffer.height) != (self._width, self._height):
            raise ValueError("CPU buffer dimensions do not match the writer")
        handle = native.SubmissionHandle()
        native.check(native.lib.mkvc_encoder_submit_cpu_buffer(
            self._handle, buffer._native_handle(), pts, ct.byref(handle)
        ))
        return Submission(handle, buffer)

    def flush(self) -> None:
        if not self._closed:
            native.check(native.lib.mkvc_encoder_flush(self._handle))

    def close(self) -> None:
        if self._closed:
            return
        result = native.lib.mkvc_encoder_close(self._handle)
        try:
            self._last_metrics = _read_metrics(
                self._handle, native.lib.mkvc_encoder_get_metrics
            )
        finally:
            native.lib.mkvc_encoder_destroy(self._handle)
            self._handle = native.EncoderHandle()
            self._closed = True
        native.check(result)

    release = close

    def __enter__(self) -> "VideoWriter":
        return self

    def __exit__(self, exc_type: object, exc: object, traceback: object) -> None:
        self.close()

    def __del__(self) -> None:
        if getattr(self, "_closed", True) is False:
            try:
                self.close()
            except Exception:
                pass


class VideoCapture(Iterator[U8Plane]):
    def __init__(
        self,
        path: str | Path,
        *,
        codec: str = "vp9",
        backend: str = "cpu",
        threads: int = 0,
        prefetch: int = 4,
        require_gpu_resident: bool = False,
    ) -> None:
        if codec not in ("vp9", "av1") or backend not in ("cpu", "intel", "nvidia"):
            raise ValueError("the Python capture supports VP9/AV1 on CPU, Intel, or NVIDIA")
        if require_gpu_resident and backend not in ("intel", "nvidia"):
            raise ValueError(
                "require_gpu_resident requires the Intel or NVIDIA backend"
            )
        if require_gpu_resident and prefetch != 0:
            raise ValueError(
                "require_gpu_resident currently requires prefetch=0"
            )
        encoded_path = str(Path(path)).encode("utf-8")
        config = native.DecoderConfig()
        config.struct_size = ct.sizeof(config)
        config.struct_version = 1
        config.input_path_utf8 = encoded_path
        config.codec = (native.MKVC_CODEC_VP9 if codec == "vp9" else
                        native.MKVC_CODEC_AV1)
        config.backend = ({"cpu": native.MKVC_BACKEND_CPU,
                           "intel": native.MKVC_BACKEND_INTEL,
                           "nvidia": native.MKVC_BACKEND_NVIDIA}[backend])
        config.threads = threads
        if prefetch < 0:
            raise ValueError("prefetch must be zero or positive")
        config.prefetch = prefetch
        self._handle = native.DecoderHandle()
        native.check(native.lib.mkvc_decoder_create(ct.byref(config), ct.byref(self._handle)))
        if require_gpu_resident:
            policy = native.CopyPolicy()
            policy.struct_size = ct.sizeof(policy)
            policy.struct_version = 1
            policy.require_gpu_resident = 1
            policy.allow_gpu_copy = 1
            policy.allow_cpu_copy = 0
            result = native.lib.mkvc_decoder_set_copy_policy(
                self._handle, ct.byref(policy)
            )
            if result != native.MKVC_OK:
                native.lib.mkvc_decoder_destroy(self._handle)
                self._handle = native.DecoderHandle()
                native.check(result)
        self._closed = False
        self._require_gpu_resident = bool(require_gpu_resident)
        self._last_metrics: PipelineMetrics | None = None
        self.last_pts_ns: int | None = None

    @property
    def metrics(self) -> PipelineMetrics:
        if self._closed:
            if self._last_metrics is None:
                raise RuntimeError("capture metrics are unavailable")
            return self._last_metrics
        return _read_metrics(self._handle, native.lib.mkvc_decoder_get_metrics)

    def _read_handle(self) -> native.FrameHandle | None:
        if self._require_gpu_resident:
            raise RuntimeError(
                "CPU frame reads are disabled by require_gpu_resident=True; "
                "use read_surface()"
            )
        if self._closed:
            raise RuntimeError("capture is closed")
        handle = native.FrameHandle()
        result = native.lib.mkvc_decoder_read(self._handle, ct.byref(handle))
        if result == native.MKVC_END_OF_STREAM:
            return None
        native.check(result)
        return handle

    @staticmethod
    def _get_view(handle: native.FrameHandle) -> native.FrameView:
        view = native.FrameView()
        view.struct_size = ct.sizeof(view)
        view.struct_version = 1
        native.check(native.lib.mkvc_frame_get_view(handle, ct.byref(view)))
        return view

    def read_i420(self) -> CpuFrame | None:
        handle = self._read_handle()
        if handle is None:
            return None
        try:
            view = self._get_view(handle)
            if view.pixel_format != native.MKVC_PIXEL_FORMAT_I420:
                raise RuntimeError("native decoder returned a non-I420 frame")
            arrays: list[U8Plane] = []
            for index in range(3):
                width = view.width if index == 0 else view.width // 2
                height = view.height if index == 0 else view.height // 2
                byte_count = view.strides[index] * height
                raw = np.ctypeslib.as_array(view.planes[index], shape=(byte_count,))
                arrays.append(
                    raw.reshape(height, view.strides[index])[:, :width].copy()
                )
            self.last_pts_ns = view.pts
            return CpuFrame(arrays[0], arrays[1], arrays[2], view.pts)
        finally:
            native.lib.mkvc_frame_release(handle)

    def read_borrowed(self, *, format: str = "i420") -> BorrowedCpuFrame | None:
        """Return read-only NumPy views sharing the native decoded allocation."""
        if format != "i420":
            raise ValueError(
                "zero-copy borrowed decode currently supports only native I420"
            )
        handle = self._read_handle()
        if handle is None:
            return None
        try:
            view = self._get_view(handle)
        except Exception:
            native.lib.mkvc_frame_release(handle)
            raise
        owned_handle = handle
        handle = native.FrameHandle()
        result = BorrowedCpuFrame(owned_handle, view)
        self.last_pts_ns = result.pts_ns
        return result

    def read_surface(self) -> GpuFrame | None:
        """Read one GPU-resident frame without a CPU readback."""
        if self._closed:
            raise RuntimeError("capture is closed")
        handle = native.GpuFrameHandle()
        result = native.lib.mkvc_decoder_read_gpu(self._handle, ct.byref(handle))
        if result == native.MKVC_END_OF_STREAM:
            return None
        native.check(result)
        return GpuFrame(handle)

    def _read_packed(self, channels: int, pixel_format: int) -> U8Plane | None:
        handle = self._read_handle()
        if handle is None:
            return None
        try:
            source = self._get_view(handle)
            destination_array = np.empty(
                (source.height, source.width, channels), dtype=np.uint8
            )
            destination = native.MutableFrameView()
            destination.struct_size = ct.sizeof(destination)
            destination.struct_version = 1
            destination.pixel_format = pixel_format
            destination.width = source.width
            destination.height = source.height
            destination.planes[0] = _plane_pointer(destination_array)
            destination.strides[0] = destination_array.strides[0]
            native.check(native.lib.mkvc_frame_copy_to(handle, ct.byref(destination)))
            self.last_pts_ns = destination.pts
            return destination_array
        finally:
            native.lib.mkvc_frame_release(handle)

    def read_processed(
        self,
        *,
        size: tuple[int, int] | None = None,
        crop: tuple[int, int, int, int] | None = None,
        fit: str = "stretch",
        rotate: int = 0,
        flip_horizontal: bool = False,
        flip_vertical: bool = False,
        background: tuple[int, int, int] = (0, 0, 0),
        format: str = "bgr",
    ) -> U8Plane | CpuFrame | tuple[U8Plane, U8Plane] | None:
        """Read and process one decoded frame through the common native plan.

        This initial implementation is CPU-resident. GPU captures return an
        explicit not-supported error instead of silently copying to the CPU.
        """
        if fit not in ("stretch", "contain", "cover"):
            raise ValueError("fit must be stretch, contain, or cover")
        if rotate not in (0, 90, 180, 270):
            raise ValueError("rotate must be 0, 90, 180, or 270")
        if format not in ("bgr", "rgb", "bgra", "i420", "nv12"):
            raise ValueError("unsupported output format")
        if any(value < 0 or value > 255 for value in background):
            raise ValueError("background components must be in [0, 255]")
        source_handle = self._read_handle()
        if source_handle is None:
            return None
        processed_handle = native.FrameHandle()
        try:
            config = native.FrameProcessConfig()
            config.struct_size = ct.sizeof(config)
            config.struct_version = 1
            config.backend = native.MKVC_BACKEND_CPU
            if crop is not None:
                config.crop_x, config.crop_y, config.crop_width, config.crop_height = crop
            if size is not None:
                config.output_width, config.output_height = size
            config.fit = {
                "stretch": native.MKVC_FRAME_FIT_STRETCH,
                "contain": native.MKVC_FRAME_FIT_CONTAIN,
                "cover": native.MKVC_FRAME_FIT_COVER,
            }[fit]
            config.rotation = rotate
            config.flip_horizontal = flip_horizontal
            config.flip_vertical = flip_vertical
            r, g, b = background
            config.background_rgba = (r << 24) | (g << 16) | (b << 8) | 255
            native.check(native.lib.mkvc_frame_process(
                source_handle, ct.byref(config), ct.byref(processed_handle)
            ))
            view = self._get_view(processed_handle)
            self.last_pts_ns = view.pts
            if format == "i420":
                arrays: list[U8Plane] = []
                for index in range(3):
                    width = view.width if index == 0 else view.width // 2
                    height = view.height if index == 0 else view.height // 2
                    raw = np.ctypeslib.as_array(
                        view.planes[index], shape=(view.strides[index] * height,)
                    )
                    arrays.append(raw.reshape(height, view.strides[index])[:, :width].copy())
                return CpuFrame(arrays[0], arrays[1], arrays[2], view.pts)
            if format == "nv12":
                y = np.empty((view.height, view.width), dtype=np.uint8)
                uv = np.empty((view.height // 2, view.width), dtype=np.uint8)
                destination = native.MutableFrameView()
                destination.struct_size = ct.sizeof(destination)
                destination.struct_version = 1
                destination.pixel_format = native.MKVC_PIXEL_FORMAT_NV12
                destination.width, destination.height = view.width, view.height
                destination.planes[0], destination.planes[1] = _plane_pointer(y), _plane_pointer(uv)
                destination.strides[0], destination.strides[1] = y.strides[0], uv.strides[0]
                native.check(native.lib.mkvc_frame_copy_to(processed_handle, ct.byref(destination)))
                return y, uv
            channels, pixel_format = {
                "bgr": (3, native.MKVC_PIXEL_FORMAT_BGR24),
                "rgb": (3, native.MKVC_PIXEL_FORMAT_RGB24),
                "bgra": (4, native.MKVC_PIXEL_FORMAT_BGRA32),
            }[format]
            output = np.empty((view.height, view.width, channels), dtype=np.uint8)
            destination = native.MutableFrameView()
            destination.struct_size = ct.sizeof(destination)
            destination.struct_version = 1
            destination.pixel_format = pixel_format
            destination.width, destination.height = view.width, view.height
            destination.planes[0] = _plane_pointer(output)
            destination.strides[0] = output.strides[0]
            native.check(native.lib.mkvc_frame_copy_to(processed_handle, ct.byref(destination)))
            return output
        finally:
            if processed_handle:
                native.lib.mkvc_frame_release(processed_handle)
            native.lib.mkvc_frame_release(source_handle)

    def read_bgr(self) -> U8Plane | None:
        return self._read_packed(3, native.MKVC_PIXEL_FORMAT_BGR24)

    def read_rgb(self) -> U8Plane | None:
        return self._read_packed(3, native.MKVC_PIXEL_FORMAT_RGB24)

    def read_bgra(self) -> U8Plane | None:
        return self._read_packed(4, native.MKVC_PIXEL_FORMAT_BGRA32)

    def read_nv12(self) -> tuple[U8Plane, U8Plane] | None:
        handle = self._read_handle()
        if handle is None:
            return None
        try:
            source = self._get_view(handle)
            y = np.empty((source.height, source.width), dtype=np.uint8)
            uv = np.empty((source.height // 2, source.width), dtype=np.uint8)
            destination = native.MutableFrameView()
            destination.struct_size = ct.sizeof(destination)
            destination.struct_version = 1
            destination.pixel_format = native.MKVC_PIXEL_FORMAT_NV12
            destination.width = source.width
            destination.height = source.height
            destination.planes[0] = _plane_pointer(y)
            destination.planes[1] = _plane_pointer(uv)
            destination.strides[0] = y.strides[0]
            destination.strides[1] = uv.strides[0]
            native.check(native.lib.mkvc_frame_copy_to(handle, ct.byref(destination)))
            self.last_pts_ns = destination.pts
            return y, uv
        finally:
            native.lib.mkvc_frame_release(handle)

    read = read_bgr

    def close(self) -> None:
        if self._closed:
            return
        result = native.lib.mkvc_decoder_close(self._handle)
        try:
            self._last_metrics = _read_metrics(
                self._handle, native.lib.mkvc_decoder_get_metrics
            )
        finally:
            native.lib.mkvc_decoder_destroy(self._handle)
            self._handle = native.DecoderHandle()
            self._closed = True
        native.check(result)

    release = close

    def __iter__(self) -> "VideoCapture":
        return self

    def __next__(self) -> U8Plane:
        frame = self.read_bgr()
        if frame is None:
            raise StopIteration
        return frame

    def __enter__(self) -> "VideoCapture":
        return self

    def __exit__(self, exc_type: object, exc: object, traceback: object) -> None:
        self.close()

    def __del__(self) -> None:
        if getattr(self, "_closed", True) is False:
            try:
                self.close()
            except Exception:
                pass
