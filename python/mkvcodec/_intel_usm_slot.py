"""Exclusive writable slot and ownership transfer for Intel USM pools."""

from __future__ import annotations

from enum import Enum, auto
from typing import Callable

from ._gpu import GpuFrame
from ._gpu_resource_reservation import _GpuResourceReservation


class _UsmPoolFrameOwner:
    """Bind one native reservation to its allocation and producer event owner."""

    def __init__(
        self,
        reservation: _GpuResourceReservation,
        allocation_owner: object,
        producer_owner: object | None,
    ) -> None:
        self.reservation = reservation
        self.allocation_owner = allocation_owner
        self.producer_owner = producer_owner

    def close(self) -> None:
        """Release reservation and external owners exactly once."""
        reservation = getattr(self, "reservation", None)
        if reservation is not None:
            reservation.close()
            self.reservation = None
            self.allocation_owner = None
            self.producer_owner = None

    def __del__(self) -> None:
        self.close()


class _SlotLeaseState(Enum):
    """Internal state of an exclusive writable USM pool reservation."""

    WRITABLE = auto()
    TRANSFERRED = auto()
    RELEASED = auto()


class IntelUsmPoolSlot:
    """Hold an exclusive writable lease over one preallocated USM resource.

    Attributes
    ----------
    pointer : int
        Device-USM allocation address.
    resource : object
        Python owner retained while the slot is writable. It becomes ``None``
        after transfer or release.
    slot_index : int
        Stable zero-based index in the pool.
    generation : int
        Reservation generation used to reject stale leases.
    """

    def __init__(
        self,
        reservation: _GpuResourceReservation,
        resource: tuple[int, object],
        *,
        context: int,
        queue: int,
        device_id: int,
        frame_size: tuple[int, int],
        pitch: int,
        dependency_registrar: Callable[[int, int], None] | None,
    ) -> None:
        self._reservation = reservation
        self.pointer, self.resource = resource
        self.context = context
        self.queue = queue
        self.device_id = device_id
        self.frame_size = frame_size
        self.pitch = pitch
        self.dependency_registrar = dependency_registrar
        self.slot_index = reservation.slot_index
        self.generation = reservation.generation
        self._state = _SlotLeaseState.WRITABLE

    def _require_writable(self) -> _GpuResourceReservation:
        """Return the reservation only while producer writes are permitted."""
        if self._state is not _SlotLeaseState.WRITABLE or self._reservation is None:
            raise RuntimeError("Intel USM pool slot is released or already imported")
        return self._reservation

    def import_frame(
        self,
        *,
        pts_ns: int = -1,
        event: int = 0,
        producer_synchronized: bool = False,
        producer_owner: object | None = None,
    ) -> GpuFrame:
        """Transfer this slot into a GPU frame after producer submission.

        Parameters
        ----------
        pts_ns : int, default: -1
            Presentation timestamp in nanoseconds.
        event : int, default: 0
            Borrowed native Level Zero producer event.
        producer_synchronized : bool, default: False
            Assert that producer work has already completed.
        producer_owner : object, optional
            Owner retaining an event-backed producer dependency.

        Returns
        -------
        GpuFrame
            Imported USM frame owning this slot reservation.
        """
        reservation = self._require_writable()
        if event and producer_owner is None:
            raise ValueError("event-backed acquisition requires producer_owner")
        owner = _UsmPoolFrameOwner(reservation, self.resource, producer_owner)
        dependency_registrar = self.dependency_registrar
        self._reservation = None
        self._state = _SlotLeaseState.TRANSFERRED
        self.resource = None
        self.dependency_registrar = None
        try:
            return GpuFrame.import_usm_nv12(
                pointer=self.pointer,
                context=self.context,
                queue=self.queue,
                device_id=self.device_id,
                frame_size=self.frame_size,
                pitch=self.pitch,
                owner=owner,
                pts_ns=pts_ns,
                event=event,
                producer_synchronized=producer_synchronized,
                dependency_registrar=dependency_registrar,
            )
        except BaseException:
            owner.close()
            raise

    def close(self) -> None:
        """Return an unimported reservation to the pool."""
        reservation = getattr(self, "_reservation", None)
        state = getattr(self, "_state", _SlotLeaseState.RELEASED)
        if state is _SlotLeaseState.WRITABLE and reservation is not None:
            reservation.close()
            self._reservation = None
            self._state = _SlotLeaseState.RELEASED
            self.resource = None
            self.dependency_registrar = None

    release = close

    def __enter__(self) -> "IntelUsmPoolSlot":
        return self

    def __exit__(self, *args: object) -> None:
        self.close()

    def __del__(self) -> None:
        self.close()
