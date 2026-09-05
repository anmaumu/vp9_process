"""Intel D3D11, VA-API, and USM external-frame import implementations."""

from __future__ import annotations

import ctypes as ct
from typing import Callable

from . import _native as native
from ._gpu_import_common import dlpack_extension, next_external_generation


def _import_d3d11_texture(
    cls, *, texture: int, fence: int, fence_value: int, device_id: int,
    frame_size: tuple[int, int], owner: object, pts_ns: int = -1,
) -> "GpuFrame":
    """Import a Windows Intel NV12 texture with a native D3D11 fence.

    Submit all producer work, Signal the target, and ensure command dispatch
    before import. Texture and fence must belong to the same device. This
    polls only the fence, retains COM references and the supplied owner, and
    never performs a CPU copy. Do not rewind the fence or write the resource
    until consumers finish. Only a single-subresource GPU-only NV12 texture
    is accepted. oneVPL encoder capability is checked separately.
    """
    if dlpack_extension() is None:
        raise RuntimeError("D3D11 import requires the mkvcodec stable-ABI extension")
    if owner is None:
        raise ValueError("D3D11 import requires a resource owner")
    if not isinstance(frame_size, tuple) or len(frame_size) != 2:
        raise ValueError("frame_size must contain width and height")
    width, height = frame_size
    if any(not isinstance(value, int) for value in
           (texture, fence, fence_value, device_id, width, height, pts_ns)):
        raise ValueError("D3D11 import descriptors must be integers")
    if (not 0 < texture <= 0xFFFFFFFFFFFFFFFF or
            not 0 < fence <= 0xFFFFFFFFFFFFFFFF or
            not 0 < fence_value < 0xFFFFFFFFFFFFFFFF or
            not 0 <= device_id <= 0xFFFFFFFFFFFFFFFF or
            not 0 < width <= 0xFFFFFFFF or not 0 < height <= 0xFFFFFFFF or
            width & 1 or height & 1 or
            not -0x8000000000000000 <= pts_ns <= 0x7FFFFFFFFFFFFFFF):
        raise ValueError("D3D11 import descriptor is invalid")
    generation = next_external_generation()
    config = native.GpuExternalFrameConfig()
    config.struct_size, config.struct_version = ct.sizeof(config), 1
    desc = config.frame
    desc.struct_size, desc.struct_version = ct.sizeof(desc), 1
    desc.backend = native.MKVC_BACKEND_INTEL
    desc.memory_type = native.MKVC_GPU_MEMORY_D3D11_TEXTURE
    desc.device_id, desc.generation = device_id, generation
    desc.pixel_format = native.MKVC_PIXEL_FORMAT_NV12
    desc.width, desc.height, desc.plane_count = width, height, 2
    desc.pts = pts_ns
    native_handle = config.native_handle
    native_handle.struct_size, native_handle.struct_version = ct.sizeof(native_handle), 1
    native_handle.type, native_handle.borrowed = native.MKVC_GPU_NATIVE_D3D11_TEXTURE, 1
    native_handle.device_id, native_handle.generation = device_id, generation
    native_handle.handles[:] = (texture, 0, fence, fence_value)
    user_data, release = dlpack_extension().external_owner_create(owner)
    config.user_data, config.release = user_data, release
    result_handle = native.GpuFrameHandle()
    try:
        native.check(native.lib.mkvc_gpu_frame_import_d3d11_fence(
            ct.byref(config), ct.byref(result_handle)))
    except Exception:
        dlpack_extension().external_owner_cancel(user_data)
        raise
    return cls(result_handle)

def _import_va_surface(
    cls, *, display: int, surface_id: int, device_id: int,
    frame_size: tuple[int, int], owner: object, pts_ns: int = -1,
    producer_synchronized: bool = False,
) -> "GpuFrame":
    """Import an Intel NV12 VA surface, retaining its owner until release.

    By default Linux uses nonblocking ``vaSyncSurface2`` polling. Submit all
    VA producer work before import; this cannot synchronize arbitrary SYCL
    or OpenCL writes. Use ``producer_synchronized=True`` only after external
    synchronization has completed. Do not explicitly close the owner while
    this lease or its encoder remains alive. The encoder retains its first
    imported owner through flush/close to preserve the borrowed VA display.
    """
    if dlpack_extension() is None:
        raise RuntimeError("VA import requires the mkvcodec stable-ABI extension")
    if owner is None:
        raise ValueError("VA import requires a resource owner")
    if not isinstance(frame_size, tuple) or len(frame_size) != 2:
        raise ValueError("frame_size must contain width and height")
    width, height = frame_size
    if any(not isinstance(value, int) for value in
           (display, surface_id, device_id, width, height, pts_ns)):
        raise ValueError("VA import descriptors must be integers")
    if (not 0 < display <= 0xFFFFFFFFFFFFFFFF or
            not 0 <= surface_id < 0xFFFFFFFF or
            not 0 <= device_id <= 0xFFFFFFFFFFFFFFFF or
            not 0 < width <= 0xFFFFFFFF or not 0 < height <= 0xFFFFFFFF or
            width & 1 or height & 1 or
            not -0x8000000000000000 <= pts_ns <= 0x7FFFFFFFFFFFFFFF):
        raise ValueError("VA import descriptor is invalid")
    generation = next_external_generation()
    config = native.GpuExternalFrameConfig()
    config.struct_size, config.struct_version = ct.sizeof(config), 1
    desc = config.frame
    desc.struct_size, desc.struct_version = ct.sizeof(desc), 1
    desc.backend = native.MKVC_BACKEND_INTEL
    desc.memory_type = native.MKVC_GPU_MEMORY_VA_SURFACE
    desc.device_id, desc.generation = device_id, generation
    desc.pixel_format = native.MKVC_PIXEL_FORMAT_NV12
    desc.width, desc.height, desc.plane_count = width, height, 2
    desc.pts = pts_ns
    handle_desc = config.native_handle
    handle_desc.struct_size, handle_desc.struct_version = ct.sizeof(handle_desc), 1
    handle_desc.type = native.MKVC_GPU_NATIVE_VA_SURFACE
    handle_desc.borrowed = 1
    handle_desc.device_id, handle_desc.generation = device_id, generation
    handle_desc.handles[0], handle_desc.handles[1] = display, surface_id
    user_data, release = dlpack_extension().external_owner_create(owner)
    config.user_data, config.release = user_data, release
    result_handle = native.GpuFrameHandle()
    try:
        importer = (native.lib.mkvc_gpu_frame_import_external
                    if producer_synchronized
                    else native.lib.mkvc_gpu_frame_import_va_surface)
        native.check(importer(ct.byref(config), ct.byref(result_handle)))
    except Exception:
        dlpack_extension().external_owner_cancel(user_data)
        raise
    return cls(result_handle)

def _import_usm_nv12(
    cls, *, pointer: int, context: int, queue: int, device_id: int,
    frame_size: tuple[int, int], pitch: int, owner: object,
    pts_ns: int = -1, event: int = 0,
    producer_synchronized: bool = False,
    dependency_registrar: Callable[[int, int], None] | None = None,
) -> "GpuFrame":
    """Import a linear Intel device-USM NV12 allocation for DLPack sharing.

    ``pointer``, ``context`` and ``queue`` must describe the same oneAPI
    device allocation. The allocation remains owned by ``owner`` until all
    frame/DLPack leases are released. The ABI does not consume a portable
    SYCL C++ event object, so callers must either pass its borrowed native
    Level Zero ``ze_event_handle_t`` or fully finish the producer queue and
    set ``producer_synchronized=True``. This representation is for external
    processing; oneVPL encode still requires a shared VA/D3D11 resource and
    any tiled-to-linear materialization is a GPU copy.
    """
    if dlpack_extension() is None:
        raise RuntimeError(
            "external USM import requires the mkvcodec stable-ABI extension"
        )
    if owner is None:
        raise ValueError("USM import requires an allocation owner")
    if dependency_registrar is not None and not callable(dependency_registrar):
        raise TypeError("dependency_registrar must be callable or None")
    if producer_synchronized == (event != 0):
        raise ValueError(
            "provide exactly one of a Level Zero event or producer_synchronized=True"
        )
    if (not isinstance(frame_size, tuple) or len(frame_size) != 2 or
            any(not isinstance(value, int) for value in frame_size)):
        raise ValueError("frame_size must contain integer width and height")
    width, height = frame_size
    values = (pointer, context, queue, event, device_id, width, height, pitch, pts_ns)
    if any(not isinstance(value, int) for value in values):
        raise ValueError("USM import descriptors must be integers")
    if (pointer <= 0 or context <= 0 or queue <= 0 or device_id < 0 or
            width <= 0 or height <= 0 or width & 1 or height & 1 or
            pitch < width or pitch > 0xFFFFFFFF or
            event < 0 or any(value > 0xFFFFFFFFFFFFFFFF for value in
                (pointer, context, queue, event, device_id)) or
            width > 0xFFFFFFFF or height > 0xFFFFFFFF or
            pts_ns < -0x8000000000000000 or pts_ns > 0x7FFFFFFFFFFFFFFF):
        raise ValueError("USM import descriptor is invalid")
    generation = next_external_generation()
    config = native.GpuExternalFrameConfig()
    config.struct_size, config.struct_version = ct.sizeof(config), 1
    desc = config.frame
    desc.struct_size, desc.struct_version = ct.sizeof(desc), 1
    desc.backend = native.MKVC_BACKEND_INTEL
    desc.memory_type = native.MKVC_GPU_MEMORY_USM
    desc.device_id, desc.generation = device_id, generation
    desc.pixel_format = native.MKVC_PIXEL_FORMAT_NV12
    desc.width, desc.height, desc.plane_count = width, height, 2
    desc.plane_offsets[1] = pitch * height
    desc.pitches[0] = desc.pitches[1] = pitch
    desc.pts = pts_ns
    handle_desc = config.native_handle
    handle_desc.struct_size, handle_desc.struct_version = ct.sizeof(handle_desc), 1
    handle_desc.type = native.MKVC_GPU_NATIVE_USM_POINTER
    handle_desc.borrowed = 1
    handle_desc.device_id, handle_desc.generation = device_id, generation
    handle_desc.handles[0], handle_desc.handles[1] = pointer, context
    handle_desc.handles[2] = queue
    handle_desc.handles[3] = event
    user_data, release = dlpack_extension().external_owner_create(owner)
    config.release, config.user_data = release, user_data
    result_handle = native.GpuFrameHandle()
    try:
        importer = (native.lib.mkvc_gpu_frame_import_external
                    if producer_synchronized
                    else native.lib.mkvc_gpu_frame_import_level_zero_event)
        native.check(importer(ct.byref(config), ct.byref(result_handle)))
    except Exception:
        dlpack_extension().external_owner_cancel(user_data)
        raise
    return cls(result_handle, dependency_registrar)
