from __future__ import annotations

import ctypes as ct
from dataclasses import dataclass
from fractions import Fraction
from pathlib import Path
from typing import Iterator

import numpy as np
import numpy.typing as npt

from . import _native as native

U8Plane = npt.NDArray[np.uint8]


@dataclass(frozen=True)
class CpuFrame:
    y: U8Plane
    u: U8Plane
    v: U8Plane
    pts_ns: int


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


def _read_metrics(handle: ct.c_void_p, function: object) -> PipelineMetrics:
    metrics = native.PipelineMetrics()
    metrics.struct_size = ct.sizeof(metrics)
    metrics.struct_version = 1
    native.check(function(handle, ct.byref(metrics)))
    paths = {0: "unknown", 1: "cpu", 2: "zero_copy"}
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
    ) -> None:
        if codec not in ("vp9", "av1") or backend not in ("cpu", "intel"):
            raise ValueError("the Python writer supports VP9/AV1 on CPU or Intel")
        width, height = frame_size
        rate = _fps_fraction(fps)
        encoded_path = str(Path(path)).encode("utf-8")
        config = native.EncoderConfig()
        config.struct_size = ct.sizeof(config)
        config.struct_version = 1
        config.output_path_utf8 = encoded_path
        config.codec = (native.MKVC_CODEC_VP9 if codec == "vp9" else
                        native.MKVC_CODEC_AV1)
        config.backend = (native.MKVC_BACKEND_CPU if backend == "cpu" else
                          native.MKVC_BACKEND_INTEL)
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
        self._width = width
        self._height = height
        self._closed = False
        self._last_metrics: PipelineMetrics | None = None

    @property
    def metrics(self) -> PipelineMetrics:
        if self._closed:
            if self._last_metrics is None:
                raise RuntimeError("writer metrics are unavailable")
            return self._last_metrics
        return _read_metrics(self._handle, native.lib.mkvc_encoder_get_metrics)

    def _submit(self, frame: native.FrameView, *, block: bool) -> bool:
        function = (native.lib.mkvc_encoder_write_frame if block else
                    native.lib.mkvc_encoder_try_write_frame)
        result = function(self._handle, ct.byref(frame))
        if result == native.MKVC_WOULD_BLOCK:
            return False
        native.check(result)
        return True

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
    ) -> None:
        if codec not in ("vp9", "av1") or backend not in ("cpu", "intel", "nvidia"):
            raise ValueError("the Python capture supports VP9/AV1 on CPU, Intel, or NVIDIA")
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
        self._closed = False
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
