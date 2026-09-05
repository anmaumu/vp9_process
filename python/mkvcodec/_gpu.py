"""GPU frame leases and zero-copy external processing interop."""

from __future__ import annotations

import ctypes as ct
import itertools
from typing import Callable

from . import _native as native
from ._gpu_interop import describe_interop
from ._gpu_plane import GpuPlane
from ._types import GpuInteropInfo

try:
    from . import _dlpack
except ImportError:
    _dlpack = None

_external_gpu_generations = itertools.count(1)


class GpuFrame:
    """Own a lease over a backend-resident GPU video frame.

    The frame retains its decoder, external owner, and producer-completion
    dependency until all native and DLPack consumers release their leases.

    Notes
    -----
    ``descriptor`` and ``native_handle`` expose borrowed metadata. They do not
    transfer ownership of the underlying CUDA, D3D11, VA-API, or USM resource.
    """
    def __init__(
        self, handle: native.GpuFrameHandle,
        dependency_registrar: Callable[[int, int], None] | None = None,
    ) -> None:
        self._handle = handle
        self._closed = False
        self._dependency_registrar = dependency_registrar

    @classmethod
    def import_d3d11_texture(
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
        if _dlpack is None:
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
        generation = next(_external_gpu_generations)
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
        user_data, release = _dlpack.external_owner_create(owner)
        config.user_data, config.release = user_data, release
        result_handle = native.GpuFrameHandle()
        try:
            native.check(native.lib.mkvc_gpu_frame_import_d3d11_fence(
                ct.byref(config), ct.byref(result_handle)))
        except Exception:
            _dlpack.external_owner_cancel(user_data)
            raise
        return cls(result_handle)

    @classmethod
    def import_va_surface(
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
        if _dlpack is None:
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
        generation = next(_external_gpu_generations)
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
        user_data, release = _dlpack.external_owner_create(owner)
        config.user_data, config.release = user_data, release
        result_handle = native.GpuFrameHandle()
        try:
            importer = (native.lib.mkvc_gpu_frame_import_external
                        if producer_synchronized
                        else native.lib.mkvc_gpu_frame_import_va_surface)
            native.check(importer(ct.byref(config), ct.byref(result_handle)))
        except Exception:
            _dlpack.external_owner_cancel(user_data)
            raise
        return cls(result_handle)

    @classmethod
    def import_cuda_pointer(
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
        if _dlpack is None:
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
        generation = next(_external_gpu_generations)
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
        user_data, release = _dlpack.external_owner_create(owner)
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
            _dlpack.external_owner_cancel(user_data)
            raise
        return cls(result_handle)

    @classmethod
    def import_usm_nv12(
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
        if _dlpack is None:
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
        generation = next(_external_gpu_generations)
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
        user_data, release = _dlpack.external_owner_create(owner)
        config.release, config.user_data = release, user_data
        result_handle = native.GpuFrameHandle()
        try:
            importer = (native.lib.mkvc_gpu_frame_import_external
                        if producer_synchronized
                        else native.lib.mkvc_gpu_frame_import_level_zero_event)
            native.check(importer(ct.byref(config), ct.byref(result_handle)))
        except Exception:
            _dlpack.external_owner_cancel(user_data)
            raise
        return cls(result_handle, dependency_registrar)

    @classmethod
    def import_dlpack_nv12(
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
        if _dlpack is None:
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
        pointer, device_id, pitch, owner = _dlpack.consume_nv12(
            tensor, width, height
        )
        return cls.import_cuda_pointer(
            pointer=pointer, context=context, device_id=device_id,
            frame_size=frame_size, pitch=pitch, owner=owner, pts_ns=pts_ns,
            stream=stream, event=event,
            producer_synchronized=producer_synchronized,
        )

    @classmethod
    def import_cuda_array(
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
        if _dlpack is None:
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
        generation = next(_external_gpu_generations)
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
        user_data, release = _dlpack.external_owner_create(owner)
        config.release, config.user_data = release, user_data
        result_handle = native.GpuFrameHandle()
        try:
            importer = (native.lib.mkvc_gpu_frame_import_external
                        if producer_synchronized
                        else native.lib.mkvc_gpu_frame_import_cuda_event)
            native.check(importer(ct.byref(config), ct.byref(result_handle)))
        except Exception:
            _dlpack.external_owner_cancel(user_data)
            raise
        return cls(result_handle)

    @property
    def descriptor(self) -> dict[str, object]:
        """dict
        Return normalized backend, layout, generation, and timestamp metadata.

        Raises
        ------
        RuntimeError
            If the frame was released or native descriptor retrieval fails.
        """
        if self._closed:
            raise RuntimeError("GPU frame is released")
        value = native.GpuFrameDesc()
        value.struct_size, value.struct_version = ct.sizeof(value), 1
        native.check(native.lib.mkvc_gpu_frame_get_desc(self._handle, ct.byref(value)))
        return {
            "backend": value.backend, "memory_type": value.memory_type,
            "device_id": value.device_id, "generation": value.generation,
            "pixel_format": value.pixel_format, "width": value.width,
            "height": value.height, "plane_count": value.plane_count,
            "plane_offsets": tuple(value.plane_offsets),
            "pitches": tuple(value.pitches), "pts_ns": value.pts,
        }

    @property
    def native_handle(self) -> dict[str, object]:
        """dict
        Return borrowed native resource and synchronization handles.

        Notes
        -----
        The returned integers do not extend the resource lifetime. Keep this
        ``GpuFrame`` alive while an external processor uses them.
        """
        if self._closed:
            raise RuntimeError("GPU frame is released")
        value = native.GpuNativeHandleDesc()
        value.struct_size, value.struct_version = ct.sizeof(value), 1
        native.check(native.lib.mkvc_gpu_frame_get_native_handle(
            self._handle, ct.byref(value)
        ))
        return {
            "type": value.type, "borrowed": bool(value.borrowed),
            "device_id": value.device_id, "generation": value.generation,
            "handles": tuple(value.handles),
        }

    @property
    def interop(self) -> GpuInteropInfo:
        """GpuInteropInfo: Describe supported external processing adapters."""
        return describe_interop(
            self.descriptor,
            self.native_handle,
            dlpack_available=_dlpack is not None,
        )

    def supports_interop(self, interface: str) -> bool:
        """Return whether the frame can be offered to an adapter family.

        Parameters
        ----------
        interface : str
            Adapter family such as ``"cuda"``, ``"dlpack"``, ``"d3d11"``,
            ``"va_api"``, or ``"sycl_usm"``.

        Returns
        -------
        bool
            ``True`` when the current memory representation supports it.
        """
        if not isinstance(interface, str):
            raise TypeError("interface must be a string")
        return interface.lower() in self.interop.processing_interfaces

    def wait(self, timeout_ms: int = 0xFFFFFFFF) -> None:
        """Wait for the frame's producer-completion dependency.

        Parameters
        ----------
        timeout_ms : int, default: 4294967295
            Maximum wait in milliseconds. The default waits indefinitely.
        """
        if self._closed:
            raise RuntimeError("GPU frame is released")
        if timeout_ms < 0 or timeout_ms > 0xFFFFFFFF:
            raise ValueError("timeout_ms is outside uint32 range")
        native.check(native.lib.mkvc_gpu_frame_wait(self._handle, timeout_ms))

    def plane(self, index: int) -> "GpuPlane":
        """Return a GPU plane implementing the Python DLPack protocol.

        Parameters
        ----------
        index : int
            Zero-based plane index.

        Returns
        -------
        GpuPlane
            Borrowed plane view that retains this frame.
        """
        if self._closed:
            raise RuntimeError("GPU frame is released")
        descriptor = self.descriptor
        if index < 0 or index >= int(descriptor["plane_count"]):
            raise IndexError("GPU plane index is out of range")
        return GpuPlane(self, index)

    def close(self) -> None:
        """Release this frame lease after external consumers are finished."""
        if not self._closed:
            native.lib.mkvc_gpu_frame_release(self._handle)
            self._handle = native.GpuFrameHandle()
            self._dependency_registrar = None
            self._closed = True

    release = close
    def __enter__(self) -> "GpuFrame": return self
    def __exit__(self, *args: object) -> None: self.close()
    def __del__(self) -> None:
        if getattr(self, "_closed", True) is False:
            self.close()
