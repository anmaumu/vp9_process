"""Normalize backend-native GPU resources into public interop metadata."""

from __future__ import annotations

from . import _native as native
from ._types import GpuInteropInfo


def describe_interop(
    descriptor: dict[str, object],
    handle: dict[str, object],
    *,
    dlpack_available: bool,
) -> GpuInteropInfo:
    """Build stable, backend-neutral processing capability metadata."""
    backends = {
        native.MKVC_BACKEND_INTEL: "intel",
        native.MKVC_BACKEND_NVIDIA: "nvidia",
    }
    memories = {
        native.MKVC_GPU_MEMORY_D3D11_TEXTURE: "d3d11_texture",
        native.MKVC_GPU_MEMORY_VA_SURFACE: "va_surface",
        native.MKVC_GPU_MEMORY_CUDA_POINTER: "cuda_pointer",
        native.MKVC_GPU_MEMORY_CUDA_ARRAY: "cuda_array",
        native.MKVC_GPU_MEMORY_USM: "usm",
    }
    handles = {
        native.MKVC_GPU_NATIVE_D3D11_TEXTURE: "d3d11_texture",
        native.MKVC_GPU_NATIVE_VA_SURFACE: "va_surface",
        native.MKVC_GPU_NATIVE_CUDA_POINTER: "cuda_pointer",
        native.MKVC_GPU_NATIVE_CUDA_ARRAY: "cuda_array",
        native.MKVC_GPU_NATIVE_USM_POINTER: "usm_pointer",
    }
    memory = memories.get(int(descriptor["memory_type"]), "unknown")
    handle_type = handles.get(int(handle["type"]), "unknown")
    if memory == "d3d11_texture":
        interfaces, dlpack, completion = ("d3d11",), False, "d3d11_fence"
    elif memory == "va_surface":
        interfaces, dlpack, completion = ("va_api",), False, "va_surface"
    elif memory in ("cuda_pointer", "cuda_array"):
        dlpack = memory == "cuda_pointer" and dlpack_available
        interfaces = ("cuda", "dlpack") if dlpack else ("cuda",)
        completion = "cuda_event"
    elif memory == "usm":
        interfaces, dlpack = ("sycl_usm", "dlpack"), True
        completion = "level_zero_event" if int(handle["handles"][3]) else "synchronized"
    else:
        interfaces, dlpack, completion = (), False, "unknown"
    return GpuInteropInfo(
        backends.get(int(descriptor["backend"]), "unknown"),
        memory,
        handle_type,
        interfaces,
        dlpack,
        completion,
    )
