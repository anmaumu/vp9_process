"""Optional CuPy implementation of NVIDIA NV12-to-packed conversion."""

from __future__ import annotations

import time
from typing import TYPE_CHECKING

import numpy as np

from ..api.processor import GpuConversionRequest, GpuImage, GpuImageInfo
from ..native import library as native
from ._color import resolve_yuv_conversion

if TYPE_CHECKING:
    from ..api.frame import GpuFrame


_KERNEL_SOURCE = r"""
extern "C" __global__ void mkvc_nv12_to_packed_u8(
    const unsigned char* y_plane,
    long long y_pitch,
    const unsigned char* uv_plane,
    long long uv_pitch,
    unsigned char* output,
    int width,
    int height,
    int channels,
    int chw,
    int blue_first,
    float y_offset,
    float y_multiplier,
    float red_v,
    float green_u,
    float green_v,
    float blue_u) {
  const int x = blockIdx.x * blockDim.x + threadIdx.x;
  const int row = blockIdx.y * blockDim.y + threadIdx.y;
  if (x >= width || row >= height) return;

  const float y = (float)y_plane[(long long)row * y_pitch + x] - y_offset;
  const long long uv_index = (long long)(row >> 1) * uv_pitch + (x & ~1);
  const float u = (float)uv_plane[uv_index] - 128.0f;
  const float v = (float)uv_plane[uv_index + 1] - 128.0f;
  const float luma = y_multiplier * y;
  const float red = fminf(255.0f, fmaxf(0.0f, luma + red_v * v));
  const float green = fminf(255.0f, fmaxf(0.0f, luma + green_u * u + green_v * v));
  const float blue = fminf(255.0f, fmaxf(0.0f, luma + blue_u * u));
  const unsigned char c0 = (unsigned char)(blue_first ? blue + 0.5f : red + 0.5f);
  const unsigned char c1 = (unsigned char)(green + 0.5f);
  const unsigned char c2 = (unsigned char)(blue_first ? red + 0.5f : blue + 0.5f);

  const long long pixel = (long long)row * width + x;
  if (chw) {
    const long long plane_size = (long long)width * height;
    output[pixel] = c0;
    output[plane_size + pixel] = c1;
    output[2 * plane_size + pixel] = c2;
    if (channels == 4) output[3 * plane_size + pixel] = 255;
  } else {
    const long long base = pixel * channels;
    output[base] = c0;
    output[base + 1] = c1;
    output[base + 2] = c2;
    if (channels == 4) output[base + 3] = 255;
  }
}
"""


class NvidiaCupyProcessorAdapter:
    """Convert NVIDIA linear NV12 frames with an asynchronous CuPy kernel.

    Notes
    -----
    CuPy is imported only when this adapter is constructed. The adapter supports
    uint8 HWC/CHW RGB, BGR, RGBA and BGRA output. Decoder completion is inserted
    into the current CUDA stream by the source DLPack export; a recorded output
    event protects source leases during asynchronous execution.
    """

    name = "nvidia-cupy"
    backends = ("nvidia",)

    def __init__(self) -> None:
        import cupy as cp

        self._cp = cp
        self._kernel = cp.RawKernel(_KERNEL_SOURCE, "mkvc_nv12_to_packed_u8")

    def supports(self, source: "GpuFrame", request: GpuConversionRequest) -> bool:
        """Return whether source is linear NVIDIA NV12 and request is uint8."""
        descriptor = source.descriptor
        return (
            source.interop.backend == "nvidia"
            and source.interop.dlpack_export
            and int(descriptor["memory_type"]) == native.MKVC_GPU_MEMORY_CUDA_POINTER
            and int(descriptor["pixel_format"]) == native.MKVC_PIXEL_FORMAT_NV12
            and int(descriptor["plane_count"]) == 2
            and request.dtype == "uint8"
        )

    def convert(self, source: "GpuFrame", request: GpuConversionRequest) -> GpuImage:
        """Schedule NV12 conversion on CuPy's current CUDA stream."""
        cp = self._cp
        descriptor = source.descriptor
        width = int(descriptor["width"])
        height = int(descriptor["height"])
        channels = 3 if request.format in ("rgb", "bgr") else 4
        shape = (
            (height, width, channels)
            if request.layout == "hwc"
            else (channels, height, width)
        )
        stream = cp.cuda.get_current_stream()
        y_plane = cp.from_dlpack(source.plane(0))
        uv_plane = cp.from_dlpack(source.plane(1))
        output = cp.empty(shape, dtype=cp.uint8)
        color_space, color_range, coefficients = resolve_yuv_conversion(
            request.color_space, request.color_range, height
        )
        block = (16, 16, 1)
        grid = ((width + 15) // 16, (height + 15) // 16, 1)
        self._kernel(
            grid,
            block,
            (
                y_plane,
                np.int64(y_plane.strides[0]),
                uv_plane,
                np.int64(uv_plane.strides[0]),
                output,
                np.int32(width),
                np.int32(height),
                np.int32(channels),
                np.int32(request.layout == "chw"),
                np.int32(request.format in ("bgr", "bgra")),
                *(np.float32(value) for value in coefficients),
            ),
            stream=stream,
        )
        completion = cp.cuda.Event(disable_timing=True)
        completion.record(stream)

        def wait(timeout_ms: int) -> None:
            if timeout_ms == 0xFFFFFFFF:
                completion.synchronize()
                return
            deadline = time.monotonic() + timeout_ms / 1000.0
            while not completion.query():
                if time.monotonic() >= deadline:
                    raise TimeoutError("CUDA RGB conversion did not complete before timeout")
                time.sleep(0.001)

        def release() -> None:
            # Source DLPack planes must outlive the kernel even when the caller
            # closes an unconsumed image immediately after conversion.
            completion.synchronize()

        info = GpuImageInfo(
            backend="nvidia",
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
            completion="cuda_event",
            copy_path="gpu_copy",
        )
        return GpuImage(
            output,
            info,
            wait=wait,
            release=release,
            owners=(y_plane, uv_plane, completion),
        )


__all__ = ["NvidiaCupyProcessorAdapter"]
