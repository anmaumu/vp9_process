"""Compatibility imports for :mod:`mkvcodec.interop.intel`."""

from .interop.intel import _import_d3d11_texture, _import_usm_nv12, _import_va_surface

__all__ = ["_import_d3d11_texture", "_import_usm_nv12", "_import_va_surface"]
