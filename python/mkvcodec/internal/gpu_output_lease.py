"""Reference-counted output leases for pooled GPU DLPack providers."""

from __future__ import annotations

import threading
from typing import Callable


class SharedGpuOutputLease:
    """Keep a pooled GPU output reserved across Python and DLPack leases.

    The initial token belongs to :class:`~mkvcodec.api.processor.GpuImage`.
    Each DLPack export obtains another token that is retained by the managed
    tensor deleter. The pool release callback therefore runs only after both
    the Python image and every consumer tensor have released their ownership.
    """

    def __init__(
        self,
        *,
        wait: Callable[[int], None],
        close_completion: Callable[[], None],
        release_slot: Callable[[], None],
    ) -> None:
        self._wait = wait
        self._close_completion = close_completion
        self._release_slot = release_slot
        self._lock = threading.Lock()
        self._references = 0
        self._released = False

    def retain(self) -> "GpuOutputLeaseToken":
        """Create one independently releasable ownership token."""
        with self._lock:
            if self._released:
                raise RuntimeError("GPU output lease is released")
            self._references += 1
        return GpuOutputLeaseToken(self)

    def wait(self, timeout_ms: int) -> None:
        """Wait for producer completion without changing ownership."""
        with self._lock:
            if self._released:
                raise RuntimeError("GPU output lease is released")
            wait = self._wait
        wait(timeout_ms)

    def _release_reference(self) -> None:
        cleanup = None
        with self._lock:
            if self._references <= 0:
                return
            self._references -= 1
            if self._references == 0:
                self._released = True
                cleanup = (
                    self._wait,
                    self._close_completion,
                    self._release_slot,
                )
                self._wait = lambda _timeout_ms: None
                self._close_completion = lambda: None
                self._release_slot = lambda: None
        if cleanup is None:
            return
        wait, close_completion, release_slot = cleanup
        failure: BaseException | None = None
        try:
            wait(0xFFFFFFFF)
        except BaseException as exception:
            failure = exception
        try:
            close_completion()
        except BaseException as exception:
            if failure is None:
                failure = exception
        try:
            release_slot()
        except BaseException as exception:
            if failure is None:
                failure = exception
        if failure is not None:
            raise failure


class GpuOutputLeaseToken:
    """One reference to a shared pooled GPU output reservation."""

    def __init__(self, shared: SharedGpuOutputLease) -> None:
        self._shared = shared

    def close(self) -> None:
        """Release this token exactly once."""
        shared = getattr(self, "_shared", None)
        if shared is not None:
            self._shared = None
            shared._release_reference()

    def __del__(self) -> None:
        try:
            self.close()
        except Exception:
            pass


class RetainedDLPackProvider:
    """Delegate DLPack while retaining a pool token in its managed deleter."""

    def __init__(self, provider: object, lease: SharedGpuOutputLease, extension: object) -> None:
        self._provider = provider
        self._lease = lease
        self._extension = extension

    def __dlpack_device__(self) -> tuple[int, int]:
        """Return the wrapped provider's standard DLPack device tuple."""
        device = self._provider.__dlpack_device__()
        return int(device[0]), int(device[1])

    def __dlpack__(self, **arguments: object) -> object:
        """Wait for production and transfer a lease token into the capsule."""
        self._lease.wait(0xFFFFFFFF)
        token = self._lease.retain()
        try:
            capsule = self._provider.__dlpack__(**arguments)
            return self._extension.retain_owner(capsule, token)
        except BaseException:
            token.close()
            raise


__all__ = [
    "GpuOutputLeaseToken",
    "RetainedDLPackProvider",
    "SharedGpuOutputLease",
]
