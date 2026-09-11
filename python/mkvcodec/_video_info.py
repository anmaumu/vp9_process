"""Input video metadata conversion and public probing."""

from __future__ import annotations

import ctypes as ct
from pathlib import Path

from . import _native as native
from ._types import VideoInfo


def _convert(value: native.VideoInfo) -> VideoInfo:
    """Convert one native versioned video-info structure."""
    codec = {
        native.MKVC_CODEC_VP9: "vp9",
        native.MKVC_CODEC_AV1: "av1",
    }.get(int(value.codec))
    if codec is None:
        raise RuntimeError("native input probe returned an unknown codec")
    fps = (
        float(value.fps_num) / float(value.fps_den)
        if value.fps_known and value.fps_den
        else None
    )
    return VideoInfo(
        codec=codec,
        width=int(value.width),
        height=int(value.height),
        fps=fps,
        duration_ns=int(value.duration_ns) if value.duration_known else None,
        frame_count=int(value.frame_count) if value.frame_count_known else None,
    )


def _empty_native_info() -> native.VideoInfo:
    """Construct the supported native video-info structure version."""
    value = native.VideoInfo()
    value.struct_size = ct.sizeof(value)
    value.struct_version = 1
    return value


def probe_video(path: str | Path) -> VideoInfo:
    """Inspect a WebM/Matroska video track without decoding pixels.

    Parameters
    ----------
    path : str or pathlib.Path
        Input WebM or Matroska path.

    Returns
    -------
    VideoInfo
        Detected codec, dimensions and available timing/count metadata.

    Notes
    -----
    Exact frame counting scans block metadata for the selected track, but does
    not decode or copy compressed frame payloads.
    """
    value = _empty_native_info()
    encoded_path = str(Path(path)).encode("utf-8")
    native.check(native.lib.mkvc_probe_input(encoded_path, ct.byref(value)))
    return _convert(value)


def read_decoder_info(handle: native.DecoderHandle) -> VideoInfo:
    """Return metadata retained by an open native decoder."""
    value = _empty_native_info()
    native.check(native.lib.mkvc_decoder_get_info(handle, ct.byref(value)))
    return _convert(value)
