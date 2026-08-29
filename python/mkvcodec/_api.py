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
    ) -> None:
        if codec != "vp9" or backend != "cpu":
            raise ValueError("the current Python slice supports codec='vp9', backend='cpu'")
        width, height = frame_size
        rate = _fps_fraction(fps)
        encoded_path = str(Path(path)).encode("utf-8")
        config = native.EncoderConfig()
        config.struct_size = ct.sizeof(config)
        config.struct_version = 1
        config.output_path_utf8 = encoded_path
        config.codec = native.MKVC_CODEC_VP9
        config.backend = native.MKVC_BACKEND_CPU
        config.width = width
        config.height = height
        config.fps_num = rate.numerator
        config.fps_den = rate.denominator
        config.quality = quality
        config.keyframe_interval_frames = keyframe_interval_frames
        config.threads = threads
        self._handle = native.EncoderHandle()
        native.check(native.lib.mkvc_encoder_create(ct.byref(config), ct.byref(self._handle)))
        self._width = width
        self._height = height
        self._closed = False

    def write_i420(self, y: U8Plane, u: U8Plane, v: U8Plane, *, pts: int = -1) -> None:
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
        native.check(native.lib.mkvc_encoder_write_frame(self._handle, ct.byref(frame)))

    def write(self, frame: tuple[U8Plane, U8Plane, U8Plane], *, pts: int = -1) -> None:
        if not isinstance(frame, tuple) or len(frame) != 3:
            raise ValueError("current write() expects an (Y, U, V) I420 tuple")
        self.write_i420(*frame, pts=pts)

    def flush(self) -> None:
        if not self._closed:
            native.check(native.lib.mkvc_encoder_flush(self._handle))

    def close(self) -> None:
        if self._closed:
            return
        try:
            native.check(native.lib.mkvc_encoder_close(self._handle))
        finally:
            native.lib.mkvc_encoder_destroy(self._handle)
            self._handle = native.EncoderHandle()
            self._closed = True

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


class VideoCapture(Iterator[CpuFrame]):
    def __init__(
        self,
        path: str | Path,
        *,
        codec: str = "vp9",
        backend: str = "cpu",
        threads: int = 0,
    ) -> None:
        if codec != "vp9" or backend != "cpu":
            raise ValueError("the current Python slice supports codec='vp9', backend='cpu'")
        encoded_path = str(Path(path)).encode("utf-8")
        config = native.DecoderConfig()
        config.struct_size = ct.sizeof(config)
        config.struct_version = 1
        config.input_path_utf8 = encoded_path
        config.codec = native.MKVC_CODEC_VP9
        config.backend = native.MKVC_BACKEND_CPU
        config.threads = threads
        self._handle = native.DecoderHandle()
        native.check(native.lib.mkvc_decoder_create(ct.byref(config), ct.byref(self._handle)))
        self._closed = False

    def read_i420(self) -> CpuFrame | None:
        if self._closed:
            raise RuntimeError("capture is closed")
        handle = native.FrameHandle()
        result = native.lib.mkvc_decoder_read(self._handle, ct.byref(handle))
        if result == native.MKVC_END_OF_STREAM:
            return None
        native.check(result)
        try:
            view = native.FrameView()
            view.struct_size = ct.sizeof(view)
            view.struct_version = 1
            native.check(native.lib.mkvc_frame_get_view(handle, ct.byref(view)))
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
            return CpuFrame(arrays[0], arrays[1], arrays[2], view.pts)
        finally:
            native.lib.mkvc_frame_release(handle)

    read = read_i420

    def close(self) -> None:
        if self._closed:
            return
        try:
            native.check(native.lib.mkvc_decoder_close(self._handle))
        finally:
            native.lib.mkvc_decoder_destroy(self._handle)
            self._handle = native.DecoderHandle()
            self._closed = True

    release = close

    def __iter__(self) -> "VideoCapture":
        return self

    def __next__(self) -> CpuFrame:
        frame = self.read_i420()
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
