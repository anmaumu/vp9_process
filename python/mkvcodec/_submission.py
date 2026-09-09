"""Asynchronous encoder-submission completion and ownership."""

from __future__ import annotations

import ctypes as ct

from . import _native as native


class Submission:
    """Retain asynchronously borrowed Python input until native completion.

    Attributes
    ----------
    done : bool
        Whether native processing has completed.
    """

    def __init__(self, handle: native.SubmissionHandle, owner: object) -> None:
        self._handle = handle
        self._owner: object | None = owner

    @property
    def done(self) -> bool:
        """bool: Whether native processing has completed."""
        if not self._handle:
            return True
        status = ct.c_uint32()
        native.check(native.lib.mkvc_submission_query(self._handle, ct.byref(status)))
        if status.value != native.MKVC_SUBMISSION_PENDING:
            self._owner = None
            return True
        return False

    def wait(self, timeout_ms: int = 0xFFFFFFFF) -> None:
        """Wait for native processing to complete.

        Parameters
        ----------
        timeout_ms : int, default: 4294967295
            Maximum wait in milliseconds. The default waits indefinitely.

        Raises
        ------
        RuntimeError
            If native processing fails or the wait times out.
        ValueError
            If ``timeout_ms`` is outside the uint32 range.
        """
        if not self._handle:
            return
        if timeout_ms < 0 or timeout_ms > 0xFFFFFFFF:
            raise ValueError("timeout_ms is outside uint32 range")
        result = native.lib.mkvc_submission_wait(self._handle, timeout_ms)
        if result != native.MKVC_ERROR_TIMEOUT:
            self._owner = None
        native.check(result)

    def close(self) -> None:
        """Release the completion handle and retained input owner."""
        if self._handle:
            native.lib.mkvc_submission_release(self._handle)
            self._handle = native.SubmissionHandle()
            self._owner = None

    release = close

    def __enter__(self) -> "Submission":
        return self

    def __exit__(self, *args: object) -> None:
        self.close()

    def __del__(self) -> None:
        if getattr(self, "_handle", None):
            self.close()
