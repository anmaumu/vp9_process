"""NVIDIA CUDA pointer, array, and DLPack import implementations."""

from __future__ import annotations

from typing import TYPE_CHECKING

from . import _native as native
from ._gpu_import_common import (
    dlpack_extension,
    import_external_frame,
    make_nv12_external_config,
)
from ._gpu_import_validation import (
    frame_size as validate_frame_size,
    validate_cuda_array,
    validate_cuda_pointer,
    validate_nv12_dimensions,
)

if TYPE_CHECKING:
    from ._gpu import GpuFrame


def _import_cuda_pointer(
    cls,
    *,
    pointer: int,
    context: int,
    device_id: int,
    frame_size: tuple[int, int],
    pitch: int,
    owner: object,
    pts_ns: int = -1,
    stream: int = 0,
    event: int = 0,
    producer_synchronized: bool = False,
) -> "GpuFrame":
    """Import a contiguous CUDA-pointer NV12 resource.

    ``owner`` is retained entirely by the stable-ABI extension until the
    final native lease is released. Pass a producer-recorded CUDA ``event``
    for asynchronous dependency tracking, or explicitly assert that the
    producer is already complete with ``producer_synchronized=True``.
    """
    if dlpack_extension() is None:
        raise RuntimeError("external CUDA import requires the mkvcodec stable-ABI extension")
    if not producer_synchronized and event <= 0:
        raise ValueError("event is required unless producer_synchronized=True")
    width, height = frame_size
    validate_cuda_pointer(
        pointer=pointer,
        context=context,
        device_id=device_id,
        width=width,
        height=height,
        pitch=pitch,
        stream=stream,
        event=event,
        pts_ns=pts_ns,
    )
    config = make_nv12_external_config(
        backend=native.MKVC_BACKEND_NVIDIA,
        memory_type=native.MKVC_GPU_MEMORY_CUDA_POINTER,
        native_type=native.MKVC_GPU_NATIVE_CUDA_POINTER,
        device_id=device_id,
        width=width,
        height=height,
        pts_ns=pts_ns,
        pitch=pitch,
        handles=(pointer, context, stream, event),
    )
    importer = (
        native.lib.mkvc_gpu_frame_import_external
        if producer_synchronized
        else native.lib.mkvc_gpu_frame_import_cuda_event
    )
    return import_external_frame(cls, config=config, owner=owner, importer=importer)


def _import_dlpack_nv12(
    cls,
    tensor: object,
    *,
    context: int,
    frame_size: tuple[int, int],
    pts_ns: int = -1,
    stream: int = 0,
    event: int = 0,
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
        raise RuntimeError("DLPack import requires the mkvcodec stable-ABI extension")
    if not isinstance(context, int) or context <= 0 or context > 0xFFFFFFFFFFFFFFFF:
        raise ValueError("context must be a nonzero CUDA context pointer")
    width, height = validate_frame_size(frame_size, integers=True)
    validate_nv12_dimensions(width, height)
    pointer, device_id, pitch, owner = dlpack_extension().consume_nv12(tensor, width, height)
    return cls.import_cuda_pointer(
        pointer=pointer,
        context=context,
        device_id=device_id,
        frame_size=frame_size,
        pitch=pitch,
        owner=owner,
        pts_ns=pts_ns,
        stream=stream,
        event=event,
        producer_synchronized=producer_synchronized,
    )


def _import_cuda_array(
    cls,
    *,
    array: int,
    context: int,
    device_id: int,
    frame_size: tuple[int, int],
    owner: object,
    pts_ns: int = -1,
    stream: int = 0,
    event: int = 0,
    producer_synchronized: bool = False,
) -> "GpuFrame":
    """Import one byte-wide CUDA array containing contiguous NV12 rows.

    The array must have ``width`` columns and ``height * 3 // 2`` rows.
    CUDA-array shape/channel validation is performed by the producer and
    NVENC driver because DLPack-style metadata is unavailable for CUarray.
    """
    if dlpack_extension() is None:
        raise RuntimeError("external CUDA import requires the mkvcodec stable-ABI extension")
    if not producer_synchronized and event <= 0:
        raise ValueError("event is required unless producer_synchronized=True")
    width, height = validate_frame_size(frame_size, integers=True)
    validate_cuda_array(
        array=array,
        context=context,
        device_id=device_id,
        width=width,
        height=height,
        stream=stream,
        event=event,
        pts_ns=pts_ns,
    )
    config = make_nv12_external_config(
        backend=native.MKVC_BACKEND_NVIDIA,
        memory_type=native.MKVC_GPU_MEMORY_CUDA_ARRAY,
        native_type=native.MKVC_GPU_NATIVE_CUDA_ARRAY,
        device_id=device_id,
        width=width,
        height=height,
        pts_ns=pts_ns,
        pitch=width,
        handles=(array, context, stream, event),
    )
    importer = (
        native.lib.mkvc_gpu_frame_import_external
        if producer_synchronized
        else native.lib.mkvc_gpu_frame_import_cuda_event
    )
    return import_external_frame(cls, config=config, owner=owner, importer=importer)
