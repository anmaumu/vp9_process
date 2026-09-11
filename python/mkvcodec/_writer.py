"""OpenCV-style video writer over the native C ABI."""

from __future__ import annotations

import ctypes as ct
from collections.abc import Sequence
from pathlib import Path

from . import _native as native
from ._cpu import CpuBuffer, Submission
from ._encoder_config import build_encoder_config
from ._frame_views import (
    FrameInput,
    make_borrowed_view,
    make_i420_view,
    make_nv12_view,
    make_packed_view,
)
from ._gpu import GpuFrame
from ._io_common import _read_metrics
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
        config, backend = build_encoder_config(
            path,
            codec=codec,
            backend=backend,
            fps=fps,
            frame_size=frame_size,
            quality=quality,
            keyframe_interval_frames=keyframe_interval_frames,
            threads=threads,
            queue_size=queue_size,
            require_gpu_resident=require_gpu_resident,
        )
        width, height = frame_size
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
        frame = make_i420_view(
            y, u, v, width=self._width, height=self._height, pts=pts
        )
        return self._submit(frame, block=block)

    def write_i420(self, y: U8Plane, u: U8Plane, v: U8Plane, *, pts: int = -1) -> None:
        """Submit one I420 frame, copying its three planes before return."""
        self._write_i420(y, u, v, pts=pts, block=True)

    def _write_nv12(self, y: U8Plane, uv: U8Plane, *, pts: int, block: bool) -> bool:
        if self._closed:
            raise RuntimeError("writer is closed")
        frame = make_nv12_view(
            y, uv, width=self._width, height=self._height, pts=pts
        )
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
        frame = make_packed_view(
            array,
            width=self._width,
            height=self._height,
            channels=channels,
            pixel_format=pixel_format,
            pts=pts,
        )
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

    def write_batch(
        self,
        frames: Sequence[FrameInput],
        *,
        pts: Sequence[int] | None = None,
    ) -> int:
        """Submit a sequence of BGR or I420 frames in order.

        Parameters
        ----------
        frames : sequence
            Packed BGR arrays or ``(Y, U, V)`` I420 tuples.
        pts : sequence of int, optional
            Per-frame timestamps. When omitted, the encoder generates them.

        Returns
        -------
        int
            Number of frames submitted successfully.

        Raises
        ------
        ValueError
            If the timestamp count differs from the frame count.
        """
        if pts is not None and len(pts) != len(frames):
            raise ValueError("pts length must match frames length")
        for index, frame in enumerate(frames):
            self.write(frame, pts=-1 if pts is None else pts[index])
        return len(frames)

    def _make_borrowed_view(
        self,
        frame: FrameInput,
        *,
        format: str = "bgr",
        pts: int = -1,
    ) -> tuple[native.FrameView, tuple[U8Plane, ...]]:
        if self._closed:
            raise RuntimeError("writer is closed")
        return make_borrowed_view(
            frame,
            format=format,
            width=self._width,
            height=self._height,
            pts=pts,
        )

    def write_borrowed(
        self,
        frame: FrameInput,
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
        frame: FrameInput,
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
