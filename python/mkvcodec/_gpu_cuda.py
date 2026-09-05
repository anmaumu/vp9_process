"""NVIDIA CUDA pointer, array, and DLPack import implementations."""

from __future__ import annotations

import ctypes as ct

from . import _native as native
from ._gpu_import_common import dlpack_extension, next_external_generation


def _import_cuda_pointer(
    cls, *, pointer: int, context: int, device_id: int,
    frame_size: tuple[int, int], pitch: int, owner: object,
    pts_ns: int = -1, stream: int = 0, event: int = 0,
    producer_synchronized: bool = False,
) -> "GpuFrame":
    """Import a contiguous CUDA-pointer NV12 resource.

    ``owner`` is retained entirely by the stable-ABI extension until the
    final native lease is released. Pass a producer-recorded CUDA ``event``
    for asynchronous dependency tracking, or explicitly assert that the
    producer is already complete with ``producer_synchronized=True``.
    """
    if dlpack_extension() is None:
        raise RuntimeError(
            "external CUDA import requires the mkvcodec stable-ABI extension"
        )
    if not producer_synchronized and event <= 0:
        raise ValueError("event is required unless producer_synchronized=True")
    width, height = frame_size
    values = (pointer, context, device_id, width, height, pitch, stream, event)
    if any(not isinstance(value, int) for value in values):
        raise ValueError("CUDA import descriptors must be integers")
    if (pointer <= 0 or context <= 0 or device_id < 0 or width <= 0 or
            height <= 0 or width & 1 or height & 1 or pitch < width or
            stream < 0 or event < 0 or
            any(value > 0xFFFFFFFFFFFFFFFF for value in
                (pointer, context, device_id, pitch, stream, event)) or
            width > 0xFFFFFFFF or height > 0xFFFFFFFF or
            pts_ns < -0x8000000000000000 or pts_ns > 0x7FFFFFFFFFFFFFFF):
        raise ValueError("CUDA import descriptor is invalid")
    generation = next_external_generation()
    config = native.GpuExternalFrameConfig()
    config.struct_size, config.struct_version = ct.sizeof(config), 1
    desc = config.frame
    desc.struct_size, desc.struct_version = ct.sizeof(desc), 1
    desc.backend = native.MKVC_BACKEND_NVIDIA
    desc.memory_type = native.MKVC_GPU_MEMORY_CUDA_POINTER
    desc.device_id, desc.generation = device_id, generation
    desc.pixel_format = native.MKVC_PIXEL_FORMAT_NV12
    desc.width, desc.height, desc.plane_count = width, height, 2
    desc.plane_offsets[1] = pitch * height
    desc.pitches[0] = desc.pitches[1] = pitch
    desc.pts = pts_ns
    handle_desc = config.native_handle
    handle_desc.struct_size = ct.sizeof(handle_desc)
    handle_desc.struct_version = 1
    handle_desc.type = native.MKVC_GPU_NATIVE_CUDA_POINTER
    handle_desc.borrowed = 1
    handle_desc.device_id, handle_desc.generation = device_id, generation
    handle_desc.handles[0], handle_desc.handles[1] = pointer, context
    handle_desc.handles[2], handle_desc.handles[3] = stream, event
    user_data, release = dlpack_extension().external_owner_create(owner)
    config.release = release
    config.user_data = user_data
    result_handle = native.GpuFrameHandle()
    try:
        importer = (native.lib.mkvc_gpu_frame_import_external
                    if producer_synchronized
                    else native.lib.mkvc_gpu_frame_import_cuda_event)
        native.check(importer(
            ct.byref(config), ct.byref(result_handle)
        ))
    except Exception:
        dlpack_extension().external_owner_cancel(user_data)
        raise
    return cls(result_handle)

def _import_dlpack_nv12(
    cls, tensor: object, *, context: int,
    frame_size: tuple[int, int], pts_ns: int = -1,
    stream: int = 0, event: int = 0,
    producer_synchronized: bool = False,
) -> "GpuFrame":
    """Consume one contiguous CUDA NV12 DLPack tensor.

    The tensor must be a CUDA ``uint8`` matrix shaped
    ``(height * 3 // 2, width)``. Its first-axis stride is used as the NV12
    pitch and the UV plane begins at ``pitch * height``. DLPack does not
    carry a CUDA context or producer event, so both synchronization and
    context identity remain explicit arguments.
    """
    if dlpack_extension() is None:
        raise RuntimeError(
            "DLPack import requires the mkvcodec stable-ABI extension"
        )
    if (not isinstance(context, int) or context <= 0 or
            context > 0xFFFFFFFFFFFFFFFF):
        raise ValueError("context must be a nonzero CUDA context pointer")
    if (not isinstance(frame_size, tuple) or len(frame_size) != 2 or
            any(not isinstance(value, int) for value in frame_size)):
        raise ValueError("frame_size must contain integer width and height")
    width, height = frame_size
    if width <= 0 or height <= 0 or width & 1 or height & 1:
        raise ValueError("NV12 dimensions must be positive and even")
    pointer, device_id, pitch, owner = dlpack_extension().consume_nv12(
        tensor, width, height
    )
    return cls.import_cuda_pointer(
        pointer=pointer, context=context, device_id=device_id,
        frame_size=frame_size, pitch=pitch, owner=owner, pts_ns=pts_ns,
        stream=stream, event=event,
        producer_synchronized=producer_synchronized,
    )

def _import_cuda_array(
    cls, *, array: int, context: int, device_id: int,
    frame_size: tuple[int, int], owner: object,
    pts_ns: int = -1, stream: int = 0, event: int = 0,
    producer_synchronized: bool = False,
) -> "GpuFrame":
    """Import one byte-wide CUDA array containing contiguous NV12 rows.

    The array must have ``width`` columns and ``height * 3 // 2`` rows.
    CUDA-array shape/channel validation is performed by the producer and
    NVENC driver because DLPack-style metadata is unavailable for CUarray.
    """
    if dlpack_extension() is None:
        raise RuntimeError(
            "external CUDA import requires the mkvcodec stable-ABI extension"
        )
    if not producer_synchronized and event <= 0:
        raise ValueError("event is required unless producer_synchronized=True")
    if (not isinstance(frame_size, tuple) or len(frame_size) != 2 or
            any(not isinstance(value, int) for value in frame_size)):
        raise ValueError("frame_size must contain integer width and height")
    width, height = frame_size
    values = (array, context, device_id, width, height, stream, event)
    if any(not isinstance(value, int) for value in values):
        raise ValueError("CUDA import descriptors must be integers")
    if (array <= 0 or context <= 0 or device_id < 0 or width <= 0 or
            height <= 0 or width & 1 or height & 1 or stream < 0 or event < 0 or
            any(value > 0xFFFFFFFFFFFFFFFF for value in
                (array, context, device_id, stream, event)) or
            width > 0xFFFFFFFF or height > 0xFFFFFFFF or
            pts_ns < -0x8000000000000000 or pts_ns > 0x7FFFFFFFFFFFFFFF):
        raise ValueError("CUDA array import descriptor is invalid")
    generation = next_external_generation()
    config = native.GpuExternalFrameConfig()
    config.struct_size, config.struct_version = ct.sizeof(config), 1
    desc = config.frame
    desc.struct_size, desc.struct_version = ct.sizeof(desc), 1
    desc.backend = native.MKVC_BACKEND_NVIDIA
    desc.memory_type = native.MKVC_GPU_MEMORY_CUDA_ARRAY
    desc.device_id, desc.generation = device_id, generation
    desc.pixel_format = native.MKVC_PIXEL_FORMAT_NV12
    desc.width, desc.height, desc.plane_count = width, height, 2
    desc.plane_offsets[1] = width * height
    desc.pitches[0] = desc.pitches[1] = width
    desc.pts = pts_ns
    handle_desc = config.native_handle
    handle_desc.struct_size, handle_desc.struct_version = ct.sizeof(handle_desc), 1
    handle_desc.type = native.MKVC_GPU_NATIVE_CUDA_ARRAY
    handle_desc.borrowed = 1
    handle_desc.device_id, handle_desc.generation = device_id, generation
    handle_desc.handles[0], handle_desc.handles[1] = array, context
    handle_desc.handles[2], handle_desc.handles[3] = stream, event
    user_data, release = dlpack_extension().external_owner_create(owner)
    config.release, config.user_data = release, user_data
    result_handle = native.GpuFrameHandle()
    try:
        importer = (native.lib.mkvc_gpu_frame_import_external
                    if producer_synchronized
                    else native.lib.mkvc_gpu_frame_import_cuda_event)
        native.check(importer(ct.byref(config), ct.byref(result_handle)))
    except Exception:
        dlpack_extension().external_owner_cancel(user_data)
        raise
    return cls(result_handle)
