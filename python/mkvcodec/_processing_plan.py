"""Validate Python processing options and build native frame plans."""

from __future__ import annotations

import ctypes as ct

from . import _native as native


def build_process_config(
    *,
    size: tuple[int, int] | None,
    crop: tuple[int, int, int, int] | None,
    fit: str,
    rotate: int,
    flip_horizontal: bool,
    flip_vertical: bool,
    background: tuple[int, int, int],
    output_format: str,
) -> native.FrameProcessConfig:
    """Validate one request and return its versioned CPU processing config."""
    if fit not in ("stretch", "contain", "cover"):
        raise ValueError("fit must be stretch, contain, or cover")
    if rotate not in (0, 90, 180, 270):
        raise ValueError("rotate must be 0, 90, 180, or 270")
    if output_format not in ("bgr", "rgb", "bgra", "i420", "nv12"):
        raise ValueError("unsupported output format")
    if any(value < 0 or value > 255 for value in background):
        raise ValueError("background components must be in [0, 255]")

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
    red, green, blue = background
    config.background_rgba = (red << 24) | (green << 16) | (blue << 8) | 255
    return config
