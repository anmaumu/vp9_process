"""OpenCV-style video capture over the native C ABI."""

from __future__ import annotations

import ctypes as ct
import time
from pathlib import Path
from typing import Iterator

from . import _native as native
from ._capabilities import _select_backend
from ._cpu import BorrowedCpuFrame
from ._frame_outputs import copy_i420, copy_nv12, copy_packed, get_frame_view
from ._gpu import GpuFrame
from ._io_common import _read_metrics
from ._processing_plan import build_process_config
from ._types import CpuFrame, PipelineMetrics, U8Plane
from ._video_info import probe_video, read_decoder_info


class VideoCapture(Iterator[U8Plane]):
    """Decode WebM/Matroska video into CPU arrays or GPU surface leases.

    Parameters
    ----------
    path : str or pathlib.Path
        Input container path.
    codec : {"auto", "vp9", "av1"}, default: "auto"
        Video codec. ``"auto"`` detects it from the container track.
    backend : {"auto", "cpu", "intel", "nvidia"}, default: "cpu"
        Decoder implementation. ``"auto"`` selects a compatible backend.
    threads : int, default: 0
        CPU worker count, or zero for backend selection.
    prefetch : int, optional
        Number of decoded CPU frames retained ahead of the reader. Zero
        disables read-ahead. The CPU default is four; GPU-resident mode is zero.
    require_gpu_resident : bool, default: False
        Disable CPU reads and fail if GPU-resident decoding is unavailable.
    conversion_threads : int, default: 0
        Total threads used by large packed BGR/RGB/BGRA conversion. Zero uses
        the bounded automatic setting; one disables auxiliary workers.

    Attributes
    ----------
    info : VideoInfo
        Immutable codec, dimensions and available timing/count metadata.
    codec : str
        Detected ``"vp9"`` or ``"av1"`` codec.
    width, height : int
        Coded frame dimensions.
    fps : float or None
        Nominal container frame rate when known.
    duration_ns : int or None
        Container duration in nanoseconds when known.
    frame_count : int or None
        Selected-track frame count when known.
    backend : str
        Selected backend name.
    last_pts_ns : int or None
        Presentation timestamp of the most recently returned frame.
    """
    def __init__(
        self,
        path: str | Path,
        *,
        codec: str = "auto",
        backend: str = "cpu",
        threads: int = 0,
        prefetch: int | None = None,
        require_gpu_resident: bool = False,
        conversion_threads: int = 0,
    ) -> None:
        if codec not in ("auto", "vp9", "av1") or backend not in ("auto", "cpu", "intel", "nvidia"):
            raise ValueError("the Python capture supports VP9/AV1 on CPU, Intel, or NVIDIA")
        if backend == "auto":
            if codec == "auto":
                codec = probe_video(path).codec
            backend = _select_backend(codec, "decode", require_gpu_resident)
        if prefetch is None:
            prefetch = 0 if require_gpu_resident else 4
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
        config.codec = {
            "auto": native.MKVC_CODEC_AUTO,
            "vp9": native.MKVC_CODEC_VP9,
            "av1": native.MKVC_CODEC_AV1,
        }[codec]
        config.backend = ({"cpu": native.MKVC_BACKEND_CPU,
                           "intel": native.MKVC_BACKEND_INTEL,
                           "nvidia": native.MKVC_BACKEND_NVIDIA}[backend])
        config.threads = threads
        if prefetch < 0:
            raise ValueError("prefetch must be zero or positive")
        if conversion_threads < 0 or conversion_threads > 4:
            raise ValueError("conversion_threads must be between 0 and 4")
        config.prefetch = prefetch
        self._handle = native.DecoderHandle()
        native.check(native.lib.mkvc_decoder_create(ct.byref(config), ct.byref(self._handle)))
        try:
            self.info = read_decoder_info(self._handle)
            if require_gpu_resident:
                policy = native.CopyPolicy()
                policy.struct_size = ct.sizeof(policy)
                policy.struct_version = 1
                policy.require_gpu_resident = 1
                policy.allow_gpu_copy = 1
                policy.allow_cpu_copy = 0
                native.check(native.lib.mkvc_decoder_set_copy_policy(
                    self._handle, ct.byref(policy)
                ))
        except Exception:
            native.lib.mkvc_decoder_destroy(self._handle)
            self._handle = native.DecoderHandle()
            raise
        self._closed = False
        self.backend = backend
        self.codec = self.info.codec
        self.width = self.info.width
        self.height = self.info.height
        self.fps = self.info.fps
        self.duration_ns = self.info.duration_ns
        self.frame_count = self.info.frame_count
        self._require_gpu_resident = bool(require_gpu_resident)
        self._conversion_threads = int(conversion_threads)
        self._last_metrics: PipelineMetrics | None = None
        self.last_pts_ns: int | None = None

    @property
    def metrics(self) -> PipelineMetrics:
        """PipelineMetrics: Current or final decoder pipeline counters."""
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
        return get_frame_view(handle)

    def read_i420(self) -> CpuFrame | None:
        """Read one copied I420 frame, or ``None`` at end of stream."""
        handle = self._read_handle()
        if handle is None:
            return None
        try:
            view = self._get_view(handle)
            if view.pixel_format != native.MKVC_PIXEL_FORMAT_I420:
                raise RuntimeError("native decoder returned a non-I420 frame")
            self.last_pts_ns = view.pts
            return copy_i420(view)
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
            output, pts = copy_packed(
                handle,
                source,
                channels=channels,
                pixel_format=pixel_format,
                conversion_threads=self._conversion_threads,
            )
            self.last_pts_ns = pts
            return output
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
        config = build_process_config(
            size=size,
            crop=crop,
            fit=fit,
            rotate=rotate,
            flip_horizontal=flip_horizontal,
            flip_vertical=flip_vertical,
            background=background,
            output_format=format,
        )
        source_handle = self._read_handle()
        if source_handle is None:
            return None
        processed_handle = native.FrameHandle()
        try:
            native.check(native.lib.mkvc_frame_process(
                source_handle, ct.byref(config), ct.byref(processed_handle)
            ))
            view = self._get_view(processed_handle)
            self.last_pts_ns = view.pts
            if format == "i420":
                return copy_i420(view)
            if format == "nv12":
                output, _ = copy_nv12(processed_handle, view)
                return output
            channels, pixel_format = {
                "bgr": (3, native.MKVC_PIXEL_FORMAT_BGR24),
                "rgb": (3, native.MKVC_PIXEL_FORMAT_RGB24),
                "bgra": (4, native.MKVC_PIXEL_FORMAT_BGRA32),
            }[format]
            output, _ = copy_packed(
                processed_handle,
                view,
                channels=channels,
                pixel_format=pixel_format,
                conversion_threads=self._conversion_threads,
            )
            return output
        finally:
            if processed_handle:
                native.lib.mkvc_frame_release(processed_handle)
            native.lib.mkvc_frame_release(source_handle)

    def read_bgr(self) -> U8Plane | None:
        """Read one copied packed BGR frame, or ``None`` at end of stream."""
        return self._read_packed(3, native.MKVC_PIXEL_FORMAT_BGR24)

    def read_rgb(self) -> U8Plane | None:
        """Read one copied packed RGB frame, or ``None`` at end of stream."""
        return self._read_packed(3, native.MKVC_PIXEL_FORMAT_RGB24)

    def read_bgra(self) -> U8Plane | None:
        """Read one copied packed BGRA frame, or ``None`` at end of stream."""
        return self._read_packed(4, native.MKVC_PIXEL_FORMAT_BGRA32)

    def read_nv12(self) -> tuple[U8Plane, U8Plane] | None:
        """Read copied NV12 luma/chroma planes, or ``None`` at end of stream."""
        handle = self._read_handle()
        if handle is None:
            return None
        try:
            source = self._get_view(handle)
            output, pts = copy_nv12(handle, source)
            self.last_pts_ns = pts
            return output
        finally:
            native.lib.mkvc_frame_release(handle)

    def read_batch(
        self,
        max_size: int,
        timeout_ms: int = 0,
        *,
        format: str = "bgr",
    ) -> list[U8Plane | CpuFrame | tuple[U8Plane, U8Plane] | GpuFrame]:
        """Read up to ``max_size`` owned frames in presentation order.

        Parameters
        ----------
        max_size : int
            Maximum number of frames; must be positive.
        timeout_ms : int, default: 0
            Total batch assembly budget in milliseconds. Zero has no deadline.
            A positive deadline is checked between backend reads and does not
            interrupt a decode operation already in progress.
        format : {"bgr", "rgb", "bgra", "i420", "nv12", "surface"}
            Output representation for every returned frame.

        Returns
        -------
        list
            Owned CPU frames or leased GPU surfaces. The final batch may be
            shorter at timeout or end of stream; EOS returns an empty list.
        """
        if not isinstance(max_size, int) or max_size <= 0:
            raise ValueError("max_size must be a positive integer")
        if not isinstance(timeout_ms, int) or timeout_ms < 0:
            raise ValueError("timeout_ms must be a non-negative integer")
        readers = {
            "bgr": self.read_bgr,
            "rgb": self.read_rgb,
            "bgra": self.read_bgra,
            "i420": self.read_i420,
            "nv12": self.read_nv12,
            "surface": self.read_surface,
        }
        try:
            reader = readers[format]
        except KeyError as exc:
            raise ValueError("unsupported batch format") from exc
        deadline = (
            time.monotonic() + timeout_ms / 1000.0 if timeout_ms > 0 else None
        )
        frames: list[U8Plane | CpuFrame | tuple[U8Plane, U8Plane] | GpuFrame] = []
        while len(frames) < max_size:
            if frames and deadline is not None and time.monotonic() >= deadline:
                break
            frame = reader()
            if frame is None:
                break
            frames.append(frame)
        return frames

    read = read_bgr

    def close(self) -> None:
        """Stop decoding and release native decoder resources."""
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
