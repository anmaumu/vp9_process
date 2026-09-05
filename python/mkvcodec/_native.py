from __future__ import annotations

import ctypes as ct
import ctypes.util
import os
from pathlib import Path

from ._native_types import *


def _candidate_paths() -> list[str]:
    explicit = os.environ.get("MKVC_LIBRARY_PATH")
    candidates = [explicit] if explicit else []
    package_dir = Path(__file__).resolve().parent
    names = ("mkvcodec.dll", "libmkvcodec.so", "libmkvcodec.dylib")
    candidates.extend(str(package_dir / name) for name in names)
    discovered = ctypes.util.find_library("mkvcodec")
    if discovered:
        candidates.append(discovered)
    return [candidate for candidate in candidates if candidate]


def _load() -> ct.CDLL:
    errors: list[str] = []
    for candidate in _candidate_paths():
        try:
            return ct.CDLL(candidate)
        except OSError as exc:
            errors.append(f"{candidate}: {exc}")
    detail = "; ".join(errors) or "no candidate library was found"
    raise ImportError(
        "Unable to load mkvcodec native library. Set MKVC_LIBRARY_PATH. " + detail
    )


lib = _load()
EncoderHandle = ct.c_void_p
DecoderHandle = ct.c_void_p
FrameHandle = ct.c_void_p
GpuFrameHandle = ct.c_void_p
SubmissionHandle = ct.c_void_p
CpuFramePoolHandle = ct.c_void_p
CpuBufferHandle = ct.c_void_p
GpuResourcePoolHandle = ct.c_void_p
GpuResourceReservationHandle = ct.c_void_p
GpuDependencyCallback = ct.CFUNCTYPE(
    ct.c_int, ct.c_void_p, ct.c_uint64, ct.c_uint64)

# BEGIN MKVC GENERATED CTYPES SIGNATURES
from ._native_signatures import configure as _configure_signatures
_configure_signatures(lib, globals())
del _configure_signatures
# END MKVC GENERATED CTYPES SIGNATURES


def check(result: int) -> None:
    if result == MKVC_OK:
        return
    detail = lib.mkvc_get_last_error()
    message = detail.decode("utf-8", errors="replace") if detail else f"result={result}"
    if result == MKVC_ERROR_INVALID_STATE:
        raise RuntimeError(message)
    if result == MKVC_ERROR_TIMEOUT:
        raise TimeoutError(message)
    if result == MKVC_ERROR_CANCELLED:
        raise RuntimeError(message)
    raise ValueError(message)
