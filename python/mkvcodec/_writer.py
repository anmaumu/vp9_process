"""OpenCV-style video writer over the native C ABI."""

from __future__ import annotations

import ctypes as ct
from pathlib import Path

import numpy as np

from . import _native as native
from ._capabilities import _select_backend
from ._cpu import CpuBuffer, Submission
from ._gpu import GpuFrame
from ._io_common import _fps_fraction, _plane_pointer, _read_metrics
from ._types import PipelineMetrics, U8Plane


class VideoWriter:
    """Encode CPU arrays or GPU surfaces into a WebM/Matroska file.

    Parameters
    ----------
    path : str or pathlib.Path
        Output container path.
    codec : {"vp9", "av1"}, default: "vp9"
        Video codec.
    backend : {"auto", "cpu", "intel", "nvidia"}, default: "cpu"
        Encoder implementation. ``"auto"`` selects a compatible backend.
    fps : float, int, or tuple of int
        Frame rate, optionally expressed as ``(numerator, denominator)``.
    frame_size : tuple of int
        Even ``(width, height)`` dimensions in pixels.
    quality : int, default: 32
        Backend-normalized quality setting.
    keyframe_interval_frames : int, default: 0
        Keyframe interval, or zero for backend selection.
    threads : int, default: 0
        CPU worker count, or zero for backend selection.
    queue_size : int, optional
        Asynchronous CPU submission capacity.
    require_gpu_resident : bool, default: False
        Reject every path that would stage a frame through CPU memory.
    """
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
        queue_size: int | None = None,
        require_gpu_resident: bool = False,
    ) -> None:
        if codec not in ("vp9", "av1") or backend not in (
            "auto", "cpu", "intel", "nvidia"
        ):
            raise ValueError(
                "the Python writer supports VP9/AV1 on CPU, Intel or NVIDIA"
            )
        if backend == "auto":
            backend = _select_backend(codec, "encode", require_gpu_resident)
        if queue_size is None:
            queue_size = 0 if require_gpu_resident else 8
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
        self.backend = backend
        self._closed = False
        self._require_gpu_resident = bool(require_gpu_resident)
        self._last_metrics: PipelineMetrics | None = None

    @property
    def metrics(self) -> PipelineMetrics:
        """PipelineMetrics: Current or final encoder pipeline counters."""
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
        """Submit one I420 frame, copying its three planes before return."""
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
        """Submit one NV12 frame, copying its luma and interleaved chroma planes."""
        self._write_nv12(y, uv, pts=pts, block=True)

    def write_surface(self, frame: GpuFrame) -> None:
        """Submit a compatible GPU frame without a CPU pixel copy."""
        if self._closed:
            raise RuntimeError("writer is closed")
        if not isinstance(frame, GpuFrame) or not frame._handle:
            raise ValueError("frame must be an open GpuFrame")
        descriptor = frame.descriptor
        expected_backend = {"intel": native.MKVC_BACKEND_INTEL,
                            "nvidia": native.MKVC_BACKEND_NVIDIA}.get(self.backend)
        if expected_backend is None or descriptor["backend"] != expected_backend:
            raise ValueError(
                f"GPU frame backend is incompatible with the {self.backend} writer"
            )
        if (descriptor["width"], descriptor["height"]) != (self._width, self._height):
            raise ValueError("GPU frame dimensions do not match the writer")
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
        """Submit one packed BGR frame, copying its pixels before return."""
        self._write_packed(frame, 3, native.MKVC_PIXEL_FORMAT_BGR24, pts=pts)

    def write_rgb(self, frame: U8Plane, *, pts: int = -1) -> None:
        """Submit one packed RGB frame, copying its pixels before return."""
        self._write_packed(frame, 3, native.MKVC_PIXEL_FORMAT_RGB24, pts=pts)

    def write_bgra(self, frame: U8Plane, *, pts: int = -1) -> None:
        """Submit one packed BGRA frame, copying its pixels before return."""
        self._write_packed(frame, 4, native.MKVC_PIXEL_FORMAT_BGRA32, pts=pts)

    def write(
        self, frame: U8Plane | tuple[U8Plane, U8Plane, U8Plane], *, pts: int = -1
    ) -> None:
        """Submit a packed BGR array or an ``(Y, U, V)`` I420 tuple."""
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
        """Wait for queued frames and finalize pending encoder output."""
        if not self._closed:
            native.check(native.lib.mkvc_encoder_flush(self._handle))

    def cancel(self) -> None:
        """Discard queued work and wake blocked producers/submissions."""
        if not self._closed:
            native.check(native.lib.mkvc_encoder_cancel(self._handle))

    def close(self) -> None:
        """Finalize the container and release native encoder resources."""
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
