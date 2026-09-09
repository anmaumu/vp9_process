"""NumPy array type that retains a native CPU-memory lease."""

from __future__ import annotations

import numpy as np


class _BorrowedArray(np.ndarray):
    """Propagate a native-memory lease to every derived NumPy view."""

    _mkvc_lease: object | None

    def __array_finalize__(self, source: object) -> None:
        self._mkvc_lease = getattr(source, "_mkvc_lease", None)
