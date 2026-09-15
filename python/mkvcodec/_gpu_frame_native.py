"""Compatibility imports for :mod:`mkvcodec.interop.frame_native`."""

from .interop.frame_native import get_gpu_frame_descriptor, get_gpu_native_handle, wait_gpu_frame

__all__ = ["get_gpu_frame_descriptor", "get_gpu_native_handle", "wait_gpu_frame"]
