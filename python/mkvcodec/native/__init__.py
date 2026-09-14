"""Private ctypes boundary for the stable mkvcodec C ABI.

This package is an implementation detail.  High-level code should use
:mod:`mkvcodec.api` instead.
"""

from . import library

__all__ = ["library"]
