"""OpenCV-style video capture over the native C ABI."""

from __future__ import annotations

import ctypes as ct
from pathlib import Path
from typing import Iterator

import numpy as np

from . import _native as native
from ._capabilities import _select_backend
from ._cpu import BorrowedCpuFrame
from ._gpu import GpuFrame
from ._io_common import _plane_pointer, _read_metrics
from ._types import CpuFrame, PipelineMetrics, U8Plane


class VideoCapture(Iterator[U8Plane]):
    """Decode WebM/Matroska video into CPU arrays or GPU surface leases.

    Parameters
    ----------
    path : str or pathlib.Path
        Input container path.
    codec : {"vp9", "av1"}, default: "vp9"
        Expected video codec.
    backend : {"auto", "cpu", "intel", "nvidia"}, default: "cpu"
        Decoder implementation. ``"auto"`` selects a compatible backend.
    threads : int, default: 0
        CPU worker count, or zero for backend selection.
    prefetch : int, optional
        Number of decoded CPU frames retained ahead of the reader.
    require_gpu_resident : bool, default: False
        Disable CPU reads and fail if GPU-resident decoding is unavailable.

    Attributes
    ----------
    backend : str
        Selected backend name.
    last_pts_ns : int or None
        Presentation timestamp of the most recently returned frame.
    """
    def __init__(
        self,
        path: str | Path,
        *,
        codec: str = "vp9",
        backend: str = "cpu",
        threads: int = 0,
        prefetch: int | None = None,
        require_gpu_resident: bool = False,
    ) -> None:
        if codec not in ("vp9", "av1") or backend not in ("auto", "cpu", "intel", "nvidia"):
            raise ValueError("the Python capture supports VP9/AV1 on CPU, Intel, or NVIDIA")
        if backend == "auto":
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
        self.backend = backend
        self._require_gpu_resident = bool(require_gpu_resident)
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
        view = native.FrameView()
        view.struct_size = ct.sizeof(view)
        view.struct_version = 1
        native.check(native.lib.mkvc_frame_get_view(handle, ct.byref(view)))
        return view

    def read_i420(self) -> CpuFrame | None:
        """Read one copied I420 frame, or ``None`` at end of stream."""
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
