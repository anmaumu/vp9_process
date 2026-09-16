"""Fused Intel VA-NV12 to packed-USM processor with bounded pooling."""

from __future__ import annotations

import os
import threading
import time
from dataclasses import dataclass
from typing import TYPE_CHECKING

from ..api.processor import GpuConversionRequest, GpuImage, GpuImageInfo
from ..internal.gpu_output_lease import SharedGpuOutputLease
from ..native import library as native
from ._color import resolve_yuv_conversion
from .common import dlpack_extension
from .intel_opencl_runtime import IntelOpenClRuntime
from .intel_sycl_bridge import (
    ExportableUsmAllocation,
    IntelSyclBridge,
    IntelVaCarrier,
)

if TYPE_CHECKING:
    from ..api.frame import GpuFrame


@dataclass
class _OutputResource:
    """One persistent output allocation and its VA carrier alias."""

    allocation: object
    memory: object
    packed: object
    carrier: IntelVaCarrier


class _LeaseBoundUsm:
    """Expose one pooled pointer while retaining its per-use lease token."""

    def __init__(self, resource: _OutputResource, queue: object, size: int, token: object) -> None:
        self.resource = resource
        self.queue = queue
        self.size = size
        self.token = token

    @property
    def __sycl_usm_array_interface__(self) -> dict[str, object]:
        pointer = self.resource.allocation.pointer.value
        return {
            "data": (pointer, False),
            "shape": (self.size,),
            "strides": None,
            "typestr": "|u1",
            "version": 1,
            "syclobj": self.queue,
        }

    def __del__(self) -> None:
        try:
            self.token.close()
        except Exception:
            pass


class _OutputPool:
    """Bounded fixed-shape output pool with explicit backpressure."""

    def __init__(
        self,
        source: "GpuFrame",
        *,
        capacity: int,
        queue: object,
        dpctl: object,
        dpnp: object,
        bridge: IntelSyclBridge,
        channels: int,
    ) -> None:
        descriptor = source.descriptor
        self.width = int(descriptor["width"])
        self.height = int(descriptor["height"])
        self.display = int(source.native_handle["handles"][0])
        self.device_id = int(descriptor["device_id"])
        self.capacity = capacity
        self.channels = channels
        self.carrier_pitch = ((self.width * channels + 63) // 64) * 64
        self._condition = threading.Condition()
        self._free = list(range(capacity - 1, -1, -1))
        self._in_use = set()
        self._closed = False
        self._anchor = source.retain()
        self.runtime = IntelOpenClRuntime(self.display)
        self.resources: list[_OutputResource] = []
        carrier_width = self.carrier_pitch
        required = carrier_width * self.height * 3 // 2
        allocation_size = ((required + 2 * 1024**2 - 1) // (2 * 1024**2)) * (2 * 1024**2)
        try:
            for _ in range(capacity):
                allocation = ExportableUsmAllocation(queue, bridge, allocation_size)
                memory = dpctl.memory.as_usm_memory(allocation)
                view = dpnp.tensor.usm_ndarray(
                    (self.height, self.width, channels),
                    dtype="u1",
                    buffer=memory,
                    strides=(carrier_width, channels, 1),
                )
                packed = dpnp.asarray(view, copy=False)
                carrier = IntelVaCarrier(
                    self._anchor,
                    packed,
                    bridge,
                    carrier_width,
                    self.height,
                )
                self.resources.append(_OutputResource(allocation, memory, packed, carrier))
        except BaseException:
            self.close(force=True)
            raise

    def acquire(self, timeout_ms: int) -> tuple[int, _OutputResource]:
        """Reserve an output, blocking only up to the configured timeout."""
        deadline = None if timeout_ms == 0xFFFFFFFF else time.monotonic() + timeout_ms / 1000.0
        with self._condition:
            while not self._free:
                if self._closed:
                    raise RuntimeError("Intel OpenCL output pool is closed")
                if timeout_ms == 0:
                    raise RuntimeError("Intel OpenCL output pool is exhausted")
                remaining = None if deadline is None else max(0.0, deadline - time.monotonic())
                if remaining == 0.0:
                    raise TimeoutError("Intel OpenCL output pool acquisition timed out")
                self._condition.wait(remaining)
            index = self._free.pop()
            self._in_use.add(index)
            return index, self.resources[index]

    def release(self, index: int) -> None:
        """Return one completed output to the pool."""
        with self._condition:
            if index not in self._in_use:
                return
            self._in_use.remove(index)
            self._free.append(index)
            self._condition.notify()

    def close(self, *, force: bool = False) -> None:
        """Close only after all output and DLPack leases have returned."""
        with self._condition:
            if self._closed:
                return
            if self._in_use and not force:
                raise RuntimeError("Intel OpenCL output pool still has active images")
            self._closed = True
        try:
            self.runtime.close()
        finally:
            for resource in reversed(self.resources):
                resource.carrier.close()
                resource.allocation.close()
            self.resources.clear()
            self._anchor.close()


class IntelOpenClProcessorAdapter:
    """Convert decoded Intel VA surfaces with one fused OpenCL GPU kernel.

    The adapter returns immediately after queue submission. Explicit
    :meth:`GpuImage.wait`, DLPack export, or final lease release performs the
    required completion wait. A bounded fixed-capacity output pool applies
    backpressure and a DLPack managed-tensor owner prevents premature slot
    reuse after :meth:`GpuImage.close`.
    """

    name = "intel-opencl"
    backends = ("intel",)

    def __init__(
        self,
        *,
        pool_size: int | None = None,
        acquire_timeout_ms: int | None = None,
        bridge_path: str | os.PathLike[str] | None = None,
    ) -> None:
        import dpctl
        import dpctl.memory
        import dpnp
        import dpnp.tensor

        if dlpack_extension() is None:
            raise ImportError("Intel OpenCL processing requires mkvcodec._dlpack")
        if os.name == "nt":
            raise ImportError("Intel OpenCL VA processing is available only on Linux")
        if pool_size is None:
            pool_size = int(os.environ.get("MKVC_INTEL_RGB_POOL_SIZE", "3"))
        if acquire_timeout_ms is None:
            acquire_timeout_ms = int(os.environ.get("MKVC_INTEL_RGB_POOL_TIMEOUT_MS", "0"))
        if pool_size < 1 or pool_size > 64:
            raise ValueError("Intel RGB pool size must be in [1, 64]")
        if acquire_timeout_ms < 0 or acquire_timeout_ms > 0xFFFFFFFF:
            raise ValueError("Intel RGB pool timeout is outside uint32 range")
        self._dpctl = dpctl
        self._dpnp = dpnp
        self._bridge = IntelSyclBridge(bridge_path)
        self._queue = dpctl.SyclQueue("level_zero:gpu")
        self.pool_size = pool_size
        self.acquire_timeout_ms = acquire_timeout_ms
        self._pools: dict[tuple[int, int, int, int, int], _OutputPool] = {}
        self._lock = threading.Lock()
        self._closed = False

    def supports(self, source: "GpuFrame", request: GpuConversionRequest) -> bool:
        """Accept Linux Intel VA NV12 and packed uint8 HWC output requests."""
        descriptor = source.descriptor
        return (
            not self._closed
            and source.interop.backend == "intel"
            and int(descriptor["memory_type"]) == native.MKVC_GPU_MEMORY_VA_SURFACE
            and int(descriptor["pixel_format"]) == native.MKVC_PIXEL_FORMAT_NV12
            and int(descriptor["plane_count"]) == 2
            and request.dtype == "uint8"
            and request.layout == "hwc"
        )

    def _pool(self, source: "GpuFrame", channels: int) -> _OutputPool:
        descriptor = source.descriptor
        handle = source.native_handle
        key = (
            int(handle["handles"][0]),
            int(descriptor["device_id"]),
            int(descriptor["width"]),
            int(descriptor["height"]),
            channels,
        )
        with self._lock:
            if self._closed:
                raise RuntimeError("Intel OpenCL processor is closed")
            pool = self._pools.get(key)
            if pool is None:
                pool = _OutputPool(
                    source,
                    capacity=self.pool_size,
                    queue=self._queue,
                    dpctl=self._dpctl,
                    dpnp=self._dpnp,
                    bridge=self._bridge,
                    channels=channels,
                )
                self._pools[key] = pool
            return pool

    def convert(self, source: "GpuFrame", request: GpuConversionRequest) -> GpuImage:
        """Submit fused conversion without a per-frame ``clFinish``."""
        source.wait(0xFFFFFFFF)
        descriptor = source.descriptor
        width, height = int(descriptor["width"]), int(descriptor["height"])
        color_space, color_range, coefficients = resolve_yuv_conversion(
            request.color_space, request.color_range, height
        )
        channels = 4 if request.format in ("rgba", "bgra") else 3
        pool = self._pool(source, channels)
        index, resource = pool.acquire(self.acquire_timeout_ms)
        completion = None
        base_token = None
        try:
            completion = pool.runtime.submit(
                source,
                resource.carrier,
                width,
                height,
                coefficients,
                bgr=request.format in ("bgr", "bgra"),
                channels=channels,
            )
            lease = SharedGpuOutputLease(
                wait=completion.wait,
                close_completion=completion.close,
                release_slot=lambda: pool.release(index),
            )
            base_token = lease.retain()
            provider_token = lease.retain()
            bound = _LeaseBoundUsm(
                resource,
                self._queue,
                pool.carrier_pitch * height,
                provider_token,
            )
            provider_memory = self._dpctl.memory.as_usm_memory(bound)
            provider_view = self._dpnp.tensor.usm_ndarray(
                (height, width, channels),
                dtype="u1",
                buffer=provider_memory,
                strides=(pool.carrier_pitch, channels, 1),
            )
            provider = self._dpnp.asarray(provider_view, copy=False)
            info = GpuImageInfo(
                backend="intel",
                device_id=int(descriptor["device_id"]),
                width=width,
                height=height,
                format=request.format,
                layout=request.layout,
                dtype=request.dtype,
                shape=(height, width, channels),
                color_space=color_space,
                color_range=color_range,
                pts_ns=int(descriptor.get("pts_ns", -1)),
                adapter=self.name,
                completion="opencl_event",
                copy_path="gpu_copy",
            )

            def release_image() -> None:
                failure: BaseException | None = None
                try:
                    lease.wait(0xFFFFFFFF)
                except BaseException as exception:
                    failure = exception
                try:
                    base_token.close()
                except BaseException as exception:
                    if failure is None:
                        failure = exception
                if failure is not None:
                    raise failure

            return GpuImage(
                provider,
                info,
                wait=lease.wait,
                release=release_image,
                owners=(bound,),
                wait_before_dlpack=True,
            )
        except BaseException:
            if base_token is not None:
                base_token.close()
            elif completion is not None:
                completion.close()
                pool.release(index)
            else:
                pool.release(index)
            raise

    def close(self) -> None:
        """Close all shape-specific pools after outstanding images return."""
        with self._lock:
            if self._closed:
                return
            pools = tuple(self._pools.values())
            for pool in pools:
                if pool._in_use:
                    raise RuntimeError("Intel OpenCL processor still has active images")
            self._closed = True
            self._pools.clear()
        for pool in pools:
            pool.close()

    def __del__(self) -> None:
        try:
            self.close()
        except Exception:
            pass


__all__ = ["IntelOpenClProcessorAdapter"]
