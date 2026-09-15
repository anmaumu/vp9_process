"""Compatibility imports for :mod:`mkvcodec.internal.io_common`."""

from .internal.io_common import (
    _fps_fraction,
    _plane_pointer,
    _read_component_metrics,
    _read_copy_edge_metrics,
    _read_metrics,
    _read_stage_metrics,
)

__all__ = [
    "_fps_fraction",
    "_plane_pointer",
    "_read_component_metrics",
    "_read_copy_edge_metrics",
    "_read_metrics",
    "_read_stage_metrics",
]
