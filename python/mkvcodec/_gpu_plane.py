"""DLPack protocol adapter for one linear GPU frame plane."""

from __future__ import annotations

import ctypes as ct
from typing import TYPE_CHECKING

from . import _native as native

if TYPE_CHECKING:
    from ._gpu import GpuFrame


def _dlpack_extension() -> object | None:
    # Resolve lazily to preserve source-tree tests that inject the optional
    # stable-ABI extension into the compatibility GPU module after import.
    from . import _gpu

    return _gpu._dlpack


class GpuPlane:
    """Represent a zero-copy uint8 view of one linear GPU plane.

    Notes
    -----
    CUDA pointers and Intel USM implement the Python DLPack protocol. Opaque
    D3D11 textures, VA surfaces, and CUDA arrays use their native-handle APIs.
    """

    def __init__(self, frame: "GpuFrame", index: int) -> None:
        self._frame = frame
        self._index = index

    def __dlpack_device__(self) -> tuple[int, int]:
        descriptor = self._frame.descriptor
        memory_type = int(descriptor["memory_type"])
        if memory_type == native.MKVC_GPU_MEMORY_CUDA_POINTER:
            device_type = 2  # kDLCUDA
        elif memory_type == native.MKVC_GPU_MEMORY_USM:
            device_type = 14  # kDLOneAPI
        else:
            raise BufferError("this GPU surface is not a linear CUDA/USM pointer")
        return device_type, int(descriptor["device_id"])

    def __dlpack__(
        self,
        *,
        stream: int | None = None,
        max_version: tuple[int, int] | None = None,
        dl_device: tuple[int, int] | None = None,
        copy: bool | None = None,
    ) -> object:
        del max_version
        if copy:
            raise BufferError("mkvcodec DLPack planes do not implement copies")
        device = self.__dlpack_device__()
        if dl_device is not None and tuple(dl_device) != device:
            raise BufferError("requested DLPack device does not match the GPU frame")
        dlpack = _dlpack_extension()
        if dlpack is None:
            raise RuntimeError(
                "the mkvcodec stable-ABI DLPack capsule extension is not installed"
            )
        if stream is None:
            consumer_stream = 0
        elif not isinstance(stream, int) or stream < 0 or stream > 0xFFFFFFFFFFFFFFFF:
            raise ValueError("DLPack stream must be a uint64 integer or None")
        else:
            consumer_stream = stream
        managed = ct.c_void_p()
        registrar = self._frame._dependency_registrar
        handle = self._frame.native_handle
        producer_event = int(handle["handles"][3])
        if registrar is not None and producer_event and consumer_stream:
            callback_error: list[BaseException] = []

            @native.GpuDependencyCallback
            def register_dependency(
                _user_data: int, event: int, target_stream: int
            ) -> int:
                try:
                    registrar(int(event), int(target_stream))
                    return native.MKVC_OK
                except BaseException as exception:
                    callback_error.append(exception)
                    return native.MKVC_ERROR_INTERNAL

            result = native.lib.mkvc_gpu_frame_export_dlpack_with_dependency(
                self._frame._handle,
                self._index,
                consumer_stream,
                register_dependency,
                None,
                ct.byref(managed),
            )
            if callback_error:
                raise callback_error[0]
            native.check(result)
        else:
            native.check(
                native.lib.mkvc_gpu_frame_export_dlpack(
                    self._frame._handle,
                    self._index,
                    consumer_stream,
                    ct.byref(managed),
                )
            )
        try:
            return dlpack.capsule_from_address(managed.value)
        except Exception:
            # Capsule construction is the only operation after native ownership
            # transfer. Reclaim through the standard managed-tensor deleter.
            native.lib.mkvc_dlpack_managed_tensor_release(managed)
            raise
