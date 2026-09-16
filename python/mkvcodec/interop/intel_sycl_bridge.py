"""Optional Intel Level Zero allocation and VA-surface sharing bridge."""

from __future__ import annotations

import ctypes as ct
import ctypes.util
import os
from pathlib import Path


P = ct.c_void_p
U = ct.c_uint
I = ct.c_int  # noqa: E741 - mirrors VA/Level Zero's integer ABI type
U64 = ct.c_uint64


def _bind(library: object, name: str, result: object, *arguments: object):
    function = getattr(library, name)
    function.restype = result
    function.argtypes = list(arguments)
    return function


def _check(result: int, operation: str) -> None:
    if result != 0:
        raise RuntimeError(f"{operation} failed: {result}")


class _Object(ct.Structure):
    _fields_ = [("fd", I), ("size", U), ("modifier", U64)]


class _Layer(ct.Structure):
    _fields_ = [
        ("format", U),
        ("planes", U),
        ("objects", U * 4),
        ("offsets", U * 4),
        ("pitches", U * 4),
    ]


class _Prime(ct.Structure):
    _fields_ = [
        ("fourcc", U),
        ("width", U),
        ("height", U),
        ("num_objects", U),
        ("objects", _Object * 4),
        ("num_layers", U),
        ("layers", _Layer * 4),
    ]


class _ValueData(ct.Union):
    _fields_ = [("i", I), ("f", ct.c_float), ("p", P), ("fn", P)]


class _Value(ct.Structure):
    _fields_ = [("type", I), ("value", _ValueData)]


class _Attribute(ct.Structure):
    _fields_ = [("type", I), ("flags", U), ("value", _Value)]


class IntelSyclBridge:
    """Load the package-local SYCL companion without linking it into Core."""

    def __init__(self, path: str | os.PathLike[str] | None = None) -> None:
        package = Path(__file__).resolve().parent.parent
        explicit = str(path) if path is not None else os.environ.get("MKVC_INTEL_SYCL_BRIDGE_PATH")
        candidates = [explicit] if explicit else []
        candidates.extend(
            str(package / name) for name in ("libmkvc_sycl_bridge.so", "libmkvc_sycl_probe.so")
        )
        discovered = ctypes.util.find_library("mkvc_sycl_bridge")
        if discovered:
            candidates.append(discovered)
        errors = []
        for candidate in candidates:
            if not candidate:
                continue
            try:
                self.library = ct.CDLL(candidate)
                break
            except OSError as exception:
                errors.append(f"{candidate}: {exception}")
        else:
            detail = "; ".join(errors) or "no bridge candidate found"
            raise ImportError("Intel OpenCL processing requires libmkvc_sycl_bridge.so; " + detail)
        self.allocate = _bind(self.library, "mkvc_sycl_alloc_exportable", I, P, U64, ct.POINTER(P))
        self.free = _bind(self.library, "mkvc_sycl_free", I, P, P)
        self.export_fd = _bind(
            self.library,
            "mkvc_sycl_export_fd",
            I,
            P,
            P,
            ct.POINTER(I),
            ct.POINTER(U64),
            ct.POINTER(U64),
        )


class ExportableUsmAllocation:
    """Own one DMA-BUF-exportable Level Zero device-USM allocation."""

    def __init__(self, queue: object, bridge: IntelSyclBridge, size: int) -> None:
        if size <= 0:
            raise ValueError("USM allocation size must be positive")
        self.queue = queue
        self.bridge = bridge
        self.size = size
        self.pointer = P()
        _check(
            bridge.allocate(queue.addressof_ref(), size, ct.byref(self.pointer)),
            "Level Zero exportable allocation",
        )

    @property
    def __sycl_usm_array_interface__(self) -> dict[str, object]:
        """Describe the allocation for dpctl without copying it."""
        return {
            "data": (self.pointer.value, False),
            "shape": (self.size,),
            "strides": None,
            "typestr": "|u1",
            "version": 1,
            "syclobj": self.queue,
        }

    def close(self) -> None:
        """Free this allocation exactly once."""
        if getattr(self, "pointer", P()).value:
            _check(
                self.bridge.free(self.queue.addressof_ref(), self.pointer),
                "Level Zero allocation free",
            )
            self.pointer.value = None

    def __del__(self) -> None:
        try:
            self.close()
        except Exception:
            pass


class IntelVaCarrier:
    """Expose exportable USM as a linear NV12 VA carrier surface.

    The Intel OpenCL VA-sharing driver rejects external RGBA surfaces. Packed
    output therefore occupies the luma plane of a carrier whose row pitch is
    the packed image row rounded up to the VA driver's 64-byte alignment. The
    chroma plane and row padding are private and never exposed to callers.
    """

    def __init__(
        self,
        source: object,
        array: object,
        bridge: IntelSyclBridge,
        carrier_width: int,
        height: int,
    ) -> None:
        self.source = source
        self.array = array
        self.surface = U(0xFFFFFFFF)
        self.display = source.native_handle["handles"][0]
        self._va = ct.CDLL("libva.so.2")
        self._destroy = _bind(self._va, "vaDestroySurfaces", I, P, ct.POINTER(U), I)
        fd, size, offset = I(-1), U64(), U64()
        pointer = array.__sycl_usm_array_interface__["data"][0]
        _check(
            bridge.export_fd(
                array.sycl_queue.addressof_ref(),
                pointer,
                ct.byref(fd),
                ct.byref(size),
                ct.byref(offset),
            ),
            "Level Zero DMA-BUF export",
        )
        try:
            required = carrier_width * height * 3 // 2
            if size.value < offset.value + required or size.value > 0xFFFFFFFF:
                raise RuntimeError("USM allocation cannot back the VA carrier")
            prime = _Prime()
            prime.fourcc = int.from_bytes(b"NV12", "little")
            prime.width, prime.height = carrier_width, height
            prime.num_objects, prime.num_layers = 1, 1
            prime.objects[0].fd, prime.objects[0].size = fd.value, size.value
            prime.layers[0].format, prime.layers[0].planes = prime.fourcc, 2
            prime.layers[0].pitches[0] = carrier_width
            prime.layers[0].pitches[1] = carrier_width
            prime.layers[0].offsets[0] = offset.value
            prime.layers[0].offsets[1] = offset.value + carrier_width * height
            attributes = (_Attribute * 2)()
            attributes[0].type, attributes[0].flags = 6, 2
            attributes[0].value.type = 1
            attributes[0].value.value.i = 0x40000000
            attributes[1].type, attributes[1].flags = 7, 2
            attributes[1].value.type = 3
            attributes[1].value.value.p = ct.addressof(prime)
            create = _bind(
                self._va,
                "vaCreateSurfaces",
                I,
                P,
                U,
                U,
                U,
                ct.POINTER(U),
                U,
                P,
                U,
            )
            _check(
                create(
                    self.display,
                    1,
                    carrier_width,
                    height,
                    ct.byref(self.surface),
                    1,
                    attributes,
                    2,
                ),
                "VA carrier import",
            )
        finally:
            if fd.value >= 0:
                os.close(fd.value)

    def close(self) -> None:
        """Destroy the carrier before releasing its VA display anchor."""
        if getattr(self, "surface", U(0xFFFFFFFF)).value != 0xFFFFFFFF:
            _check(
                self._destroy(self.display, ct.byref(self.surface), 1),
                "VA carrier destroy",
            )
            self.surface.value = 0xFFFFFFFF
            self.source = None

    def __del__(self) -> None:
        try:
            self.close()
        except Exception:
            pass


__all__ = ["ExportableUsmAllocation", "IntelSyclBridge", "IntelVaCarrier"]
