"""Compatibility imports for :mod:`mkvcodec.internal.frame_views`."""

from .internal.frame_views import (
    FrameInput,
    make_borrowed_view,
    make_i420_view,
    make_nv12_view,
    make_packed_view,
)

__all__ = [
    "FrameInput",
    "make_borrowed_view",
    "make_i420_view",
    "make_nv12_view",
    "make_packed_view",
]
