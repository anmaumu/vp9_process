"""Shared state and optional-extension access for external GPU imports."""

from __future__ import annotations

import itertools


_external_gpu_generations = itertools.count(1)


def next_external_generation() -> int:
    """Return a process-unique generation for one external frame lease."""
    return next(_external_gpu_generations)


def dlpack_extension() -> object | None:
    """Resolve the optional extension through the compatibility GPU module."""
    from . import _gpu

    return _gpu._dlpack
