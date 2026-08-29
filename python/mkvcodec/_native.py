from __future__ import annotations

import ctypes as ct
import ctypes.util
import os
from pathlib import Path

MKVC_OK = 0
MKVC_ERROR_INVALID_STATE = 5
MKVC_END_OF_STREAM = 8
MKVC_BACKEND_CPU = 1
MKVC_CODEC_VP9 = 1
MKVC_PIXEL_FORMAT_I420 = 1
MKVC_PIXEL_FORMAT_NV12 = 2
MKVC_PIXEL_FORMAT_BGR24 = 3
MKVC_PIXEL_FORMAT_RGB24 = 4
MKVC_PIXEL_FORMAT_BGRA32 = 5


class EncoderConfig(ct.Structure):
    _fields_ = [
        ("struct_size", ct.c_uint32),
        ("struct_version", ct.c_uint32),
        ("output_path_utf8", ct.c_char_p),
        ("codec", ct.c_uint32),
        ("backend", ct.c_uint32),
        ("width", ct.c_uint32),
        ("height", ct.c_uint32),
        ("fps_num", ct.c_uint32),
        ("fps_den", ct.c_uint32),
        ("quality", ct.c_uint32),
        ("keyframe_interval_frames", ct.c_uint32),
        ("threads", ct.c_uint32),
    ]


class DecoderConfig(ct.Structure):
    _fields_ = [
        ("struct_size", ct.c_uint32),
        ("struct_version", ct.c_uint32),
        ("input_path_utf8", ct.c_char_p),
        ("codec", ct.c_uint32),
        ("backend", ct.c_uint32),
        ("threads", ct.c_uint32),
    ]


class FrameView(ct.Structure):
    _fields_ = [
        ("struct_size", ct.c_uint32),
        ("struct_version", ct.c_uint32),
        ("pixel_format", ct.c_uint32),
        ("width", ct.c_uint32),
        ("height", ct.c_uint32),
        ("planes", ct.POINTER(ct.c_uint8) * 4),
        ("strides", ct.c_int32 * 4),
        ("pts", ct.c_int64),
    ]


def _candidate_paths() -> list[str]:
    explicit = os.environ.get("MKVC_LIBRARY_PATH")
    candidates = [explicit] if explicit else []
    package_dir = Path(__file__).resolve().parent
    names = ("mkvcodec.dll", "libmkvcodec.so", "libmkvcodec.dylib")
    candidates.extend(str(package_dir / name) for name in names)
    discovered = ctypes.util.find_library("mkvcodec")
    if discovered:
        candidates.append(discovered)
    return [candidate for candidate in candidates if candidate]


def _load() -> ct.CDLL:
    errors: list[str] = []
    for candidate in _candidate_paths():
        try:
            return ct.CDLL(candidate)
        except OSError as exc:
            errors.append(f"{candidate}: {exc}")
    detail = "; ".join(errors) or "no candidate library was found"
    raise ImportError(
        "Unable to load mkvcodec native library. Set MKVC_LIBRARY_PATH. " + detail
    )


lib = _load()
EncoderHandle = ct.c_void_p
DecoderHandle = ct.c_void_p
FrameHandle = ct.c_void_p

lib.mkvc_encoder_create.argtypes = [ct.POINTER(EncoderConfig), ct.POINTER(EncoderHandle)]
lib.mkvc_encoder_create.restype = ct.c_int
lib.mkvc_encoder_write_frame.argtypes = [EncoderHandle, ct.POINTER(FrameView)]
lib.mkvc_encoder_write_frame.restype = ct.c_int
lib.mkvc_encoder_flush.argtypes = [EncoderHandle]
lib.mkvc_encoder_flush.restype = ct.c_int
lib.mkvc_encoder_close.argtypes = [EncoderHandle]
lib.mkvc_encoder_close.restype = ct.c_int
lib.mkvc_encoder_destroy.argtypes = [EncoderHandle]

lib.mkvc_decoder_create.argtypes = [ct.POINTER(DecoderConfig), ct.POINTER(DecoderHandle)]
lib.mkvc_decoder_create.restype = ct.c_int
lib.mkvc_decoder_read.argtypes = [DecoderHandle, ct.POINTER(FrameHandle)]
lib.mkvc_decoder_read.restype = ct.c_int
lib.mkvc_decoder_close.argtypes = [DecoderHandle]
lib.mkvc_decoder_close.restype = ct.c_int
lib.mkvc_decoder_destroy.argtypes = [DecoderHandle]

lib.mkvc_frame_get_view.argtypes = [FrameHandle, ct.POINTER(FrameView)]
lib.mkvc_frame_get_view.restype = ct.c_int
lib.mkvc_frame_release.argtypes = [FrameHandle]
lib.mkvc_get_last_error.restype = ct.c_char_p


def check(result: int) -> None:
    if result == MKVC_OK:
        return
    detail = lib.mkvc_get_last_error()
    message = detail.decode("utf-8", errors="replace") if detail else f"result={result}"
    if result == MKVC_ERROR_INVALID_STATE:
        raise RuntimeError(message)
    raise ValueError(message)
