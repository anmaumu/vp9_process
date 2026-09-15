"""Compatibility imports for :mod:`mkvcodec.interop.common`."""

from .interop.common import (
    dlpack_extension,
    import_external_frame,
    make_nv12_external_config,
    next_external_generation,
)

__all__ = [
    "dlpack_extension",
    "import_external_frame",
    "make_nv12_external_config",
    "next_external_generation",
]
