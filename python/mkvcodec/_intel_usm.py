"""Intel USM frame pool, slot reservations, and backpressure control."""

from __future__ import annotations

import ctypes as ct
from typing import Callable

from . import _native as native
from ._gpu import GpuFrame
from ._gpu_resource_reservation import _GpuResourceReservation
from ._intel_usm_slot import IntelUsmPoolSlot
from ._types import GpuResourcePoolStats

__all__ = ["IntelUsmFramePool", "IntelUsmPoolSlot"]


class IntelUsmFramePool:
    """Bounded pool over caller-preallocated linear Intel device-USM resources.

    ``resources`` contains ``(pointer, owner)`` pairs. The pool intentionally
    does not allocate through SYCL, keeping mkvcodec independent of its C++ ABI.
    A slot returns only after every native frame and DLPack lease releases its
    composite owner. Nonblocking acquisition reports backpressure as ``None``.

    Parameters
    ----------
    resources : sequence of tuple
        Unique ``(device_pointer, owner)`` pairs allocated by the caller.
    context, queue : int
        Borrowed native oneAPI context and queue handles.
    device_id : int
        DLPack device identifier.
    frame_size : tuple of int
        Even ``(width, height)`` dimensions for contiguous NV12 data.
    pitch : int
        Row pitch in bytes, at least the frame width.
    dependency_registrar : callable, optional
        Registers a producer event dependency on a DLPack consumer stream.
    """
    def __init__(
        self, resources: list[tuple[int, object]] | tuple[tuple[int, object], ...],
        *, context: int, queue: int, device_id: int,
        frame_size: tuple[int, int], pitch: int,
        dependency_registrar: Callable[[int, int], None] | None = None,
    ) -> None:
        if not isinstance(resources, (list, tuple)) or not resources:
            raise ValueError("resources must be a non-empty sequence")
        normalized: list[tuple[int, object]] = []
        pointers: set[int] = set()
        for resource in resources:
            if (not isinstance(resource, tuple) or len(resource) != 2 or
                    not isinstance(resource[0], int) or resource[0] <= 0 or
                    resource[1] is None):
                raise ValueError("each USM resource must be a (positive pointer, owner) tuple")
            if resource[0] in pointers:
                raise ValueError("USM pool pointers must be unique")
            pointers.add(resource[0])
            normalized.append(resource)
        if dependency_registrar is not None and not callable(dependency_registrar):
            raise TypeError("dependency_registrar must be callable or None")
        width, height = frame_size
        values = (context, queue, device_id, width, height, pitch)
        if (any(not isinstance(value, int) for value in values) or context <= 0 or
                queue <= 0 or device_id < 0 or width <= 0 or height <= 0 or
                width & 1 or height & 1 or pitch < width or pitch > 0xFFFFFFFF):
            raise ValueError("invalid Intel USM pool device or NV12 layout")
        if (context > 0xFFFFFFFFFFFFFFFF or queue > 0xFFFFFFFFFFFFFFFF or
                device_id > 0xFFFFFFFFFFFFFFFF or width > 0xFFFFFFFF or
                height > 0xFFFFFFFF or len(normalized) > 0xFFFFFFFF):
            raise ValueError("Intel USM pool values exceed their ABI range")
        config = native.GpuResourcePoolConfig()
        config.struct_size, config.struct_version = ct.sizeof(config), 1
        config.capacity = len(normalized)
        self._handle = native.GpuResourcePoolHandle()
        native.check(native.lib.mkvc_gpu_resource_pool_create(
            ct.byref(config), ct.byref(self._handle)))
        self._resources = normalized
        self.context, self.queue, self.device_id = context, queue, device_id
        self.frame_size, self.pitch = frame_size, pitch
        self.dependency_registrar = dependency_registrar

    def _reserve(self, timeout_ms: int) -> _GpuResourceReservation | None:
        if not self._handle:
            raise RuntimeError("Intel USM frame pool is closed")
        if not isinstance(timeout_ms, int) or timeout_ms < 0 or timeout_ms > 0xFFFFFFFF:
            raise ValueError("timeout_ms is outside uint32 range")
        handle = native.GpuResourceReservationHandle()
        result = native.lib.mkvc_gpu_resource_pool_acquire(
            self._handle, timeout_ms, ct.byref(handle))
        if result == native.MKVC_WOULD_BLOCK:
            return None
        native.check(result)
        return _GpuResourceReservation(handle)

    def _slot(self, reservation: _GpuResourceReservation) -> IntelUsmPoolSlot:
        return IntelUsmPoolSlot(
            reservation, self._resources[reservation.slot_index],
            context=self.context, queue=self.queue, device_id=self.device_id,
            frame_size=self.frame_size, pitch=self.pitch,
            dependency_registrar=self.dependency_registrar)

    def acquire_slot(self, timeout_ms: int = 0xFFFFFFFF) -> IntelUsmPoolSlot:
        """Reserve one resource for external producer work.

        Parameters
        ----------
        timeout_ms : int, default: 4294967295
            Maximum wait in milliseconds. The default waits indefinitely.

        Returns
        -------
        IntelUsmPoolSlot
            Exclusive writable reservation.
        """
        reservation = self._reserve(timeout_ms)
        if reservation is None:
            native.check(native.MKVC_WOULD_BLOCK)
            raise RuntimeError("unreachable")
        return self._slot(reservation)

    def try_acquire_slot(self) -> IntelUsmPoolSlot | None:
        """Reserve immediately, or return ``None`` when all slots are leased."""
        reservation = self._reserve(0)
        return None if reservation is None else self._slot(reservation)

    def acquire(
        self, *, timeout_ms: int = 0xFFFFFFFF, pts_ns: int = -1,
        event: int = 0, producer_synchronized: bool = False,
        producer_owner: object | None = None,
    ) -> GpuFrame:
        """Acquire and import one slot, blocking up to ``timeout_ms``.

        Returns
        -------
        GpuFrame
            Imported frame whose lifetime controls slot reuse.
        """
        return self.acquire_slot(timeout_ms).import_frame(
            pts_ns=pts_ns, event=event,
            producer_synchronized=producer_synchronized,
            producer_owner=producer_owner)

    def try_acquire(
        self, *, pts_ns: int = -1, event: int = 0,
        producer_synchronized: bool = False,
        producer_owner: object | None = None,
    ) -> GpuFrame | None:
        """Acquire immediately or return ``None`` without taking ownership."""
        slot = self.try_acquire_slot()
        if slot is None:
            return None
        return slot.import_frame(
            pts_ns=pts_ns, event=event,
            producer_synchronized=producer_synchronized,
            producer_owner=producer_owner)

    @property
    def stats(self) -> GpuResourcePoolStats:
        """GpuResourcePoolStats: Current capacity and backpressure counters."""
        if not self._handle:
            raise RuntimeError("Intel USM frame pool is closed")
        value = native.GpuResourcePoolStats()
        value.struct_size, value.struct_version = ct.sizeof(value), 1
        native.check(native.lib.mkvc_gpu_resource_pool_get_stats(
            self._handle, ct.byref(value)))
        return GpuResourcePoolStats(
            int(value.capacity), int(value.in_use), int(value.peak_in_use),
            int(value.acquisitions), int(value.rejected_acquisitions),
            int(value.wait_ns))

    def close(self) -> None:
        """Close the pool after all outstanding frame leases are released."""
        if getattr(self, "_handle", None):
            native.lib.mkvc_gpu_resource_pool_destroy(self._handle)
            self._handle = native.GpuResourcePoolHandle()
            self._resources = []

    release = close
    def __enter__(self) -> "IntelUsmFramePool": return self
    def __exit__(self, *args: object) -> None: self.close()
    def __del__(self) -> None: self.close()
