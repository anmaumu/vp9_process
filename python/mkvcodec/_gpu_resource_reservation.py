"""Native GPU resource-pool reservation lease."""

from __future__ import annotations

import ctypes as ct

from . import _native as native


class _GpuResourceReservation:
    """Own one native pool reservation and its generation descriptor."""

    def __init__(self, handle: native.GpuResourceReservationHandle) -> None:
        self._handle = handle
        desc = native.GpuResourceReservationDesc()
        desc.struct_size = ct.sizeof(desc)
        desc.struct_version = 1
        try:
            native.check(native.lib.mkvc_gpu_resource_reservation_get_desc(handle, ct.byref(desc)))
        except BaseException:
            native.lib.mkvc_gpu_resource_reservation_release(self._handle)
            self._handle = native.GpuResourceReservationHandle()
            raise
        self.slot_index = int(desc.slot_index)
        self.generation = int(desc.generation)

    def close(self) -> None:
        """Return this reservation to its native pool exactly once."""
        if getattr(self, "_handle", None):
            native.lib.mkvc_gpu_resource_reservation_release(self._handle)
            self._handle = native.GpuResourceReservationHandle()

    def __del__(self) -> None:
        self.close()
