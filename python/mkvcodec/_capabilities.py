"""Compatibility imports for :mod:`mkvcodec.api.backend`."""

from .api.backend import _select_backend, backend_capabilities, select_backend

__all__ = ["_select_backend", "backend_capabilities", "select_backend"]
