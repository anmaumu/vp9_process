"""Optional dpnp implementation of Intel USM NV12-to-packed conversion."""

from __future__ import annotations

import time
from typing import TYPE_CHECKING

from ..api.processor import GpuConversionRequest, GpuImage, GpuImageInfo
from ..native import library as native
from ._color import resolve_yuv_conversion

if TYPE_CHECKING:
    from ..api.frame import GpuFrame


class IntelDpnpProcessorAdapter:
    """Convert linear Intel device-USM NV12 frames with dpnp operations.

    Notes
    -----
    This adapter intentionally rejects opaque VA-API and D3D11 decode
    surfaces. Those resources require a separate GPU materialization step into
    linear USM before DLPack can represent them. Input DLPack import uses
    ``copy=False`` and all output/intermediate allocations remain on the source
    SYCL device.
    """

    name = "intel-dpnp"
    backends = ("intel",)

    def __init__(self) -> None:
        import dpnp

        self._dpnp = dpnp

    def supports(self, source: "GpuFrame", request: GpuConversionRequest) -> bool:
        """Return whether source is linear Intel USM NV12 and request is uint8."""
        descriptor = source.descriptor
        return (
            source.interop.backend == "intel"
            and source.interop.dlpack_export
            and int(descriptor["memory_type"]) == native.MKVC_GPU_MEMORY_USM
            and int(descriptor["pixel_format"]) == native.MKVC_PIXEL_FORMAT_NV12
            and int(descriptor["plane_count"]) == 2
            and request.dtype == "uint8"
        )

    def convert(self, source: "GpuFrame", request: GpuConversionRequest) -> GpuImage:
        """Schedule packed RGB conversion on the source array's SYCL queue."""
        dpnp = self._dpnp
        descriptor = source.descriptor
        width = int(descriptor["width"])
        height = int(descriptor["height"])
        channels = 3 if request.format in ("rgb", "bgr") else 4
        shape = (
            (height, width, channels)
            if request.layout == "hwc"
            else (channels, height, width)
        )
        queue = None
        intermediates: list[object] = []
        try:
            y_plane = dpnp.from_dlpack(source.plane(0), copy=False)
            uv_plane = dpnp.from_dlpack(source.plane(1), copy=False)
            queue = y_plane.sycl_queue
            if uv_plane.sycl_queue != queue:
                raise RuntimeError("Intel NV12 planes were imported on different SYCL queues")

            color_space, color_range, coefficients = resolve_yuv_conversion(
                request.color_space, request.color_range, height
            )
            y_offset, y_multiplier, red_v, green_u, green_v, blue_u = coefficients
            luma = (y_plane[:height, :width].astype(dpnp.float32) - y_offset) * y_multiplier
            u = uv_plane[: height // 2, 0:width:2].astype(dpnp.float32) - 128.0
            v = uv_plane[: height // 2, 1:width:2].astype(dpnp.float32) - 128.0
            u = dpnp.repeat(dpnp.repeat(u, 2, axis=0), 2, axis=1)
            v = dpnp.repeat(dpnp.repeat(v, 2, axis=0), 2, axis=1)
            red = dpnp.clip(luma + red_v * v, 0.0, 255.0)
            green = dpnp.clip(luma + green_u * u + green_v * v, 0.0, 255.0)
            blue = dpnp.clip(luma + blue_u * u, 0.0, 255.0)
            output = dpnp.empty(
                shape,
                dtype=dpnp.uint8,
                sycl_queue=queue,
                usm_type="device",
            )
            ordered = (
                (blue, green, red)
                if request.format in ("bgr", "bgra")
                else (red, green, blue)
            )
            packed_channels = tuple(
                (channel + 0.5).astype(dpnp.int32).astype(dpnp.uint8)
                for channel in ordered
            )
            for index, channel in enumerate(packed_channels):
                if request.layout == "hwc":
                    output[..., index] = channel
                else:
                    output[index, ...] = channel
            if channels == 4:
                if request.layout == "hwc":
                    output[..., 3] = 255
                else:
                    output[3, ...] = 255
            intermediates.extend(
                (y_plane, uv_plane, luma, u, v, red, green, blue, *packed_channels)
            )
        except BaseException:
            if queue is not None:
                queue.wait()
            raise

        def wait(timeout_ms: int) -> None:
            started = time.monotonic()
            queue.wait()
            if timeout_ms != 0xFFFFFFFF and time.monotonic() - started > timeout_ms / 1000.0:
                raise TimeoutError("SYCL RGB conversion completed after its wait timeout")

        def release() -> None:
            # dpnp's high-level operations do not expose one aggregate event.
            # Finish the queue before dropping source/intermediate USM owners.
            queue.wait()

        info = GpuImageInfo(
            backend="intel",
            device_id=int(descriptor["device_id"]),
            width=width,
            height=height,
            format=request.format,
            layout=request.layout,
            dtype=request.dtype,
            shape=shape,
            color_space=color_space,
            color_range=color_range,
            pts_ns=int(descriptor.get("pts_ns", -1)),
            adapter=self.name,
            completion="sycl_queue",
            copy_path="gpu_copy",
        )
        return GpuImage(
            output,
            info,
            wait=wait,
            release=release,
            owners=intermediates,
        )


__all__ = ["IntelDpnpProcessorAdapter"]
