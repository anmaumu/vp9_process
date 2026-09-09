"""Validate Python writer options and build native encoder configs."""

from __future__ import annotations

import ctypes as ct
from pathlib import Path

from . import _native as native
from ._capabilities import _select_backend
from ._io_common import _fps_fraction


def build_encoder_config(
    path: str | Path,
    *,
    codec: str,
    backend: str,
    fps: float | int | tuple[int, int],
    frame_size: tuple[int, int],
    quality: int,
    keyframe_interval_frames: int,
    threads: int,
    queue_size: int | None,
    require_gpu_resident: bool,
) -> tuple[native.EncoderConfig, str]:
    """Return a validated versioned config and the selected backend name."""
    if codec not in ("vp9", "av1") or backend not in (
        "auto",
        "cpu",
        "intel",
        "nvidia",
    ):
        raise ValueError("the Python writer supports VP9/AV1 on CPU, Intel or NVIDIA")
    if backend == "auto":
        backend = _select_backend(codec, "encode", require_gpu_resident)
    if queue_size is None:
        queue_size = 0 if require_gpu_resident else 8
    if require_gpu_resident and backend not in ("intel", "nvidia"):
        raise ValueError("require_gpu_resident requires the Intel or NVIDIA backend")
    if require_gpu_resident and queue_size != 0:
        raise ValueError("require_gpu_resident currently requires queue_size=0")

    width, height = frame_size
    rate = _fps_fraction(fps)
    config = native.EncoderConfig()
    config.struct_size = ct.sizeof(config)
    config.struct_version = 1
    config.output_path_utf8 = str(Path(path)).encode("utf-8")
    config.codec = native.MKVC_CODEC_VP9 if codec == "vp9" else native.MKVC_CODEC_AV1
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
    return config, backend
