"""Compatibility imports for :mod:`mkvcodec.interop.cuda`."""

from .interop.cuda import _import_cuda_array, _import_cuda_pointer, _import_dlpack_nv12

__all__ = ["_import_cuda_array", "_import_cuda_pointer", "_import_dlpack_nv12"]
