"""Runtime backend discovery and deterministic backend selection."""
from __future__ import annotations

import ctypes as ct

from . import _native as native
from ._types import BackendCapability


def backend_capabilities() -> tuple[BackendCapability, ...]:
    """Return codec capabilities reported by the loaded native library.

    Returns
    -------
    tuple of BackendCapability
        Stable Python value objects for all recognized backend/codec rows.

    Raises
    ------
    RuntimeError
        If the capability set grows between the two native query calls.
    """
    count = ct.c_size_t()
    native.check(native.lib.mkvc_get_backend_capabilities(None, ct.byref(count)))
    if count.value == 0:
        return ()
    values = (native.BackendCapability * count.value)()
    requested = count.value
    for value in values:
        value.struct_size = ct.sizeof(value)
    native.check(native.lib.mkvc_get_backend_capabilities(values, ct.byref(count)))
    if count.value > requested:
        raise RuntimeError("backend capability set changed during query")
    backend_names = {
        native.MKVC_BACKEND_CPU: "cpu",
        native.MKVC_BACKEND_INTEL: "intel",
        native.MKVC_BACKEND_NVIDIA: "nvidia",
    }
    codec_names = {native.MKVC_CODEC_VP9: "vp9", native.MKVC_CODEC_AV1: "av1"}
    result = []
    for value in values[:count.value]:
        if value.backend not in backend_names or value.codec not in codec_names:
            continue
        result.append(
            BackendCapability(
                backend_names[value.backend],
                codec_names[value.codec],
                bool(value.can_decode),
                bool(value.can_encode),
                bool(value.is_hardware),
            )
        )
    return tuple(result)


def select_backend(
    codec: str,
    *,
    decode: bool = True,
    encode: bool = True,
    require_gpu_resident: bool = False,
) -> str:
    """Select one backend satisfying every requested pipeline direction.

    Parameters
    ----------
    codec:
        ``"vp9"`` or ``"av1"``.
    decode, encode:
        Required pipeline directions. At least one must be true.
    require_gpu_resident:
        Exclude the CPU backend when true.

    Returns
    -------
    str
        Selected backend name.

    Raises
    ------
    ValueError
        If arguments do not describe a pipeline direction.
    RuntimeError
        If no runtime backend satisfies the request.
    """
    if codec not in ("vp9", "av1"):
        raise ValueError("codec must be vp9 or av1")
    if not decode and not encode:
        raise ValueError("at least one of decode or encode must be requested")
    available = {
        row.backend
        for row in backend_capabilities()
        if row.codec == codec
        and (not decode or row.can_decode)
        and (not encode or row.can_encode)
        and (not require_gpu_resident or row.is_hardware)
    }
    if encode and codec == "vp9":
        preference = ("intel", "cpu")
    else:
        preference = ("nvidia", "intel", "cpu")
    for candidate in preference:
        if candidate in available:
            return candidate
    residence = " GPU-resident" if require_gpu_resident else ""
    directions = "/".join(
        name
        for name, enabled in (("decode", decode), ("encode", encode))
        if enabled
    )
    raise RuntimeError(f"no{residence} {codec} {directions} backend is available")


def _select_backend(codec: str, direction: str, require_gpu: bool) -> str:
    """Select a backend for one internal reader or writer direction."""
    if direction not in ("decode", "encode"):
        raise ValueError("direction must be decode or encode")
    return select_backend(
        codec,
        decode=direction == "decode",
        encode=direction == "encode",
        require_gpu_resident=require_gpu,
    )
