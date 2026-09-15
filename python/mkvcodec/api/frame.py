"""GPU frame leases and zero-copy external processing interop."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Callable

import numpy as np
import numpy.typing as npt

from ..interop import dlpack as dlpack_interop
from ..interop.cuda import _import_cuda_array as _import_cuda_array_impl
from ..interop.cuda import _import_cuda_pointer as _import_cuda_pointer_impl
from ..interop.cuda import _import_dlpack_nv12 as _import_dlpack_nv12_impl
from ..interop.intel import _import_d3d11_texture as _import_d3d11_texture_impl
from ..interop.intel import _import_usm_nv12 as _import_usm_nv12_impl
from ..interop.intel import _import_va_surface as _import_va_surface_impl
from ..native import library as native
from ..interop.frame_native import (
    get_gpu_frame_descriptor,
    get_gpu_native_handle,
    wait_gpu_frame,
)
from ..interop.descriptor import describe_interop
from ..interop.dlpack import GpuPlane
from .metrics import GpuInteropInfo

U8Plane = npt.NDArray[np.uint8]


@dataclass(frozen=True)
class CpuFrame:
    """Owned decoded I420 image.

    Attributes
    ----------
    y, u, v : numpy.ndarray
        Owned ``uint8`` planes. Chroma planes have half width and height.
    pts_ns : int
        Presentation timestamp in nanoseconds.
    """

    y: U8Plane
    u: U8Plane
    v: U8Plane
    pts_ns: int

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
        return _import_d3d11_texture_impl(
            cls, texture=texture, fence=fence, fence_value=fence_value,
            device_id=device_id, frame_size=frame_size, owner=owner, pts_ns=pts_ns,
        )

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
        return _import_va_surface_impl(
            cls, display=display, surface_id=surface_id, device_id=device_id,
            frame_size=frame_size, owner=owner, pts_ns=pts_ns,
            producer_synchronized=producer_synchronized,
        )

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
        return _import_cuda_pointer_impl(
            cls, pointer=pointer, context=context, device_id=device_id,
            frame_size=frame_size, pitch=pitch, owner=owner, pts_ns=pts_ns,
            stream=stream, event=event,
            producer_synchronized=producer_synchronized,
        )

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
        return _import_usm_nv12_impl(
            cls, pointer=pointer, context=context, queue=queue, device_id=device_id,
            frame_size=frame_size, pitch=pitch, owner=owner, pts_ns=pts_ns,
            event=event, producer_synchronized=producer_synchronized,
            dependency_registrar=dependency_registrar,
        )

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
        return _import_dlpack_nv12_impl(
            cls, tensor=tensor, context=context, frame_size=frame_size,
            pts_ns=pts_ns, stream=stream, event=event,
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
        return _import_cuda_array_impl(
            cls, array=array, context=context, device_id=device_id,
            frame_size=frame_size, owner=owner, pts_ns=pts_ns, stream=stream,
            event=event, producer_synchronized=producer_synchronized,
        )

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
        return get_gpu_frame_descriptor(self._handle)

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
        return get_gpu_native_handle(self._handle)

    @property
    def interop(self) -> GpuInteropInfo:
        """GpuInteropInfo: Describe supported external processing adapters."""
        return describe_interop(
            self.descriptor,
            self.native_handle,
            dlpack_available=dlpack_interop.extension is not None,
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
        wait_gpu_frame(self._handle, timeout_ms)

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


# Imported after GpuFrame is defined because Intel pool slots refer back to it.
from ..internal.cpu_pool import BorrowedCpuFrame, CpuBuffer, CpuFramePool  # noqa: E402
from ..internal.intel_usm_pool import IntelUsmFramePool, IntelUsmPoolSlot  # noqa: E402
from ..internal.submission import Submission  # noqa: E402

__all__ = [
    "BorrowedCpuFrame",
    "CpuBuffer",
    "CpuFrame",
    "CpuFramePool",
    "GpuFrame",
    "GpuPlane",
    "IntelUsmFramePool",
    "IntelUsmPoolSlot",
    "Submission",
    "U8Plane",
]
