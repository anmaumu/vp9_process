"""Compatibility import for :mod:`mkvcodec.internal.gpu_pool`."""

from .internal.gpu_pool import _GpuResourceReservation

__all__ = ["_GpuResourceReservation"]
