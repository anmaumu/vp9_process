"""Intel D3D11, VA-API, and USM external-frame import implementations."""

from __future__ import annotations

from typing import TYPE_CHECKING, Callable

from . import _native as native
from ._gpu_import_common import (
    dlpack_extension,
    import_external_frame,
    make_nv12_external_config,
)
from ._gpu_import_validation import (
    frame_size as validate_frame_size,
    validate_d3d11,
    validate_usm,
    validate_va_surface,
)

if TYPE_CHECKING:
    from ._gpu import GpuFrame


def _import_d3d11_texture(
    cls,
    *,
    texture: int,
    fence: int,
    fence_value: int,
    device_id: int,
    frame_size: tuple[int, int],
    owner: object,
    pts_ns: int = -1,
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
    width, height = validate_frame_size(frame_size, integers=False)
    validate_d3d11(
        texture=texture,
        fence=fence,
        fence_value=fence_value,
        device_id=device_id,
        width=width,
        height=height,
        pts_ns=pts_ns,
    )
    config = make_nv12_external_config(
        backend=native.MKVC_BACKEND_INTEL,
        memory_type=native.MKVC_GPU_MEMORY_D3D11_TEXTURE,
        native_type=native.MKVC_GPU_NATIVE_D3D11_TEXTURE,
        device_id=device_id,
        width=width,
        height=height,
        pts_ns=pts_ns,
        handles=(texture, 0, fence, fence_value),
    )
    return import_external_frame(
        cls,
        config=config,
        owner=owner,
        importer=native.lib.mkvc_gpu_frame_import_d3d11_fence,
    )


def _import_va_surface(
    cls,
    *,
    display: int,
    surface_id: int,
    device_id: int,
    frame_size: tuple[int, int],
    owner: object,
    pts_ns: int = -1,
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
    width, height = validate_frame_size(frame_size, integers=False)
    validate_va_surface(
        display=display,
        surface_id=surface_id,
        device_id=device_id,
        width=width,
        height=height,
        pts_ns=pts_ns,
    )
    config = make_nv12_external_config(
        backend=native.MKVC_BACKEND_INTEL,
        memory_type=native.MKVC_GPU_MEMORY_VA_SURFACE,
        native_type=native.MKVC_GPU_NATIVE_VA_SURFACE,
        device_id=device_id,
        width=width,
        height=height,
        pts_ns=pts_ns,
        handles=(display, surface_id),
    )
    importer = (
        native.lib.mkvc_gpu_frame_import_external
        if producer_synchronized
        else native.lib.mkvc_gpu_frame_import_va_surface
    )
    return import_external_frame(cls, config=config, owner=owner, importer=importer)


def _import_usm_nv12(
    cls,
    *,
    pointer: int,
    context: int,
    queue: int,
    device_id: int,
    frame_size: tuple[int, int],
    pitch: int,
    owner: object,
    pts_ns: int = -1,
    event: int = 0,
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
        raise RuntimeError("external USM import requires the mkvcodec stable-ABI extension")
    if owner is None:
        raise ValueError("USM import requires an allocation owner")
    if dependency_registrar is not None and not callable(dependency_registrar):
        raise TypeError("dependency_registrar must be callable or None")
    if producer_synchronized == (event != 0):
        raise ValueError("provide exactly one of a Level Zero event or producer_synchronized=True")
    width, height = validate_frame_size(frame_size, integers=True)
    validate_usm(
        pointer=pointer,
        context=context,
        queue=queue,
        event=event,
        device_id=device_id,
        width=width,
        height=height,
        pitch=pitch,
        pts_ns=pts_ns,
    )
    config = make_nv12_external_config(
        backend=native.MKVC_BACKEND_INTEL,
        memory_type=native.MKVC_GPU_MEMORY_USM,
        native_type=native.MKVC_GPU_NATIVE_USM_POINTER,
        device_id=device_id,
        width=width,
        height=height,
        pts_ns=pts_ns,
        pitch=pitch,
        handles=(pointer, context, queue, event),
    )
    importer = (
        native.lib.mkvc_gpu_frame_import_external
        if producer_synchronized
        else native.lib.mkvc_gpu_frame_import_level_zero_event
    )
    return import_external_frame(
        cls,
        config=config,
        owner=owner,
        importer=importer,
        dependency_registrar=dependency_registrar,
    )
