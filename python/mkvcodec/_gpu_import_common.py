"""Shared state and optional-extension access for external GPU imports."""

from __future__ import annotations

import ctypes as ct
import itertools
from typing import TYPE_CHECKING, Callable

from . import _native as native

if TYPE_CHECKING:
    from ._gpu import GpuFrame


_external_gpu_generations = itertools.count(1)


def next_external_generation() -> int:
    """Return a process-unique generation for one external frame lease."""
    return next(_external_gpu_generations)


def dlpack_extension() -> object | None:
    """Resolve the optional extension through the compatibility GPU module."""
    from . import _gpu

    return _gpu._dlpack


def make_nv12_external_config(
    *,
    backend: int,
    memory_type: int,
    native_type: int,
    device_id: int,
    width: int,
    height: int,
    pts_ns: int,
    handles: tuple[int, ...],
    pitch: int = 0,
) -> native.GpuExternalFrameConfig:
    """Build one versioned borrowed NV12 external-frame descriptor."""
    generation = next_external_generation()
    config = native.GpuExternalFrameConfig()
    config.struct_size, config.struct_version = ct.sizeof(config), 1
    desc = config.frame
    desc.struct_size, desc.struct_version = ct.sizeof(desc), 1
    desc.backend = backend
    desc.memory_type = memory_type
    desc.device_id, desc.generation = device_id, generation
    desc.pixel_format = native.MKVC_PIXEL_FORMAT_NV12
    desc.width, desc.height, desc.plane_count = width, height, 2
    if pitch:
        desc.plane_offsets[1] = pitch * height
        desc.pitches[0] = desc.pitches[1] = pitch
    desc.pts = pts_ns

    native_handle = config.native_handle
    native_handle.struct_size, native_handle.struct_version = (
        ct.sizeof(native_handle),
        1,
    )
    native_handle.type, native_handle.borrowed = native_type, 1
    native_handle.device_id, native_handle.generation = device_id, generation
    for index, value in enumerate(handles):
        native_handle.handles[index] = value
    return config


def import_external_frame(
    cls,
    *,
    config: native.GpuExternalFrameConfig,
    owner: object,
    importer: Callable,
    dependency_registrar: Callable[[int, int], None] | None = None,
) -> "GpuFrame":
    """Transfer an external owner's lifetime into one native frame lease."""
    extension = dlpack_extension()
    if extension is None:
        raise RuntimeError("external GPU import requires the stable-ABI extension")
    user_data, release = extension.external_owner_create(owner)
    config.user_data, config.release = user_data, release
    result_handle = native.GpuFrameHandle()
    try:
        native.check(importer(ct.byref(config), ct.byref(result_handle)))
    except Exception:
        extension.external_owner_cancel(user_data)
        raise
    if dependency_registrar is None:
        return cls(result_handle)
    return cls(result_handle, dependency_registrar)
