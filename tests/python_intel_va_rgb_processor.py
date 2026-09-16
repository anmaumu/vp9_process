"""Qualify VA decode -> linear USM -> dpnp RGB values on Intel hardware."""

from __future__ import annotations

import ctypes as ct
import gc
import json
import os
import sys
import time
import weakref

import numpy as np


def skip(message: str) -> None:
    print(f"Intel VA RGB processor test skipped: {message}")
    raise SystemExit(77)


try:
    import dpctl
    import dpctl.memory
    import dpnp
    import dpnp.tensor
except Exception as exception:
    skip(f"dpctl/dpnp unavailable ({exception})")

native, extension, package, fixture, helper = sys.argv[1:6]
os.environ["MKVC_LIBRARY_PATH"] = native
sys.path[:0] = [package, extension]

import _dlpack  # noqa: E402
import mkvcodec  # noqa: E402
import mkvcodec.interop.dlpack as dlpack_api  # noqa: E402
from intel_va_opencl_support import (  # noqa: E402
    I,
    P,
    U,
    OpenClReuseSession,
    bind,
    check,
    convert_nv12_rgba,
    copy_nv12,
)
from intel_va_prime_support import Attribute, Prime  # noqa: E402
from mkvcodec.interop._color import resolve_yuv_conversion  # noqa: E402

dlpack_api.extension = _dlpack


class ExportableAllocation:
    """Own a Level Zero device allocation with DMA-BUF export enabled."""

    def __init__(self, queue: object, library: object, size: int) -> None:
        self.queue = queue
        self.size = size
        self.pointer = P()
        self._free = bind(library, "mkvc_test_sycl_free", I, P, P)
        allocate = bind(
            library,
            "mkvc_test_sycl_alloc_exportable",
            I,
            P,
            ct.c_uint64,
            ct.POINTER(P),
        )
        check(allocate(queue.addressof_ref(), size, ct.byref(self.pointer)))

    @property
    def __sycl_usm_array_interface__(self) -> dict[str, object]:
        return {
            "data": (self.pointer.value, False),
            "shape": (self.size,),
            "strides": None,
            "typestr": "|u1",
            "version": 1,
            "syclobj": self.queue,
        }

    def __del__(self) -> None:
        if getattr(self, "pointer", P()).value:
            check(self._free(self.queue.addressof_ref(), self.pointer))
            self.pointer.value = None


class UsmVaOwner:
    """Share one exportable USM allocation as a linear NV12 VA surface."""

    def __init__(
        self,
        source: object,
        array: object,
        library: object,
        width: int,
        height: int,
    ) -> None:
        self.source = source
        self.array = array
        self.surface = U(0xFFFFFFFF)
        self.display = source.native_handle["handles"][0]
        va = ct.CDLL("libva.so.2")
        self._destroy = bind(va, "vaDestroySurfaces", I, P, ct.POINTER(U), I)
        export = bind(
            library,
            "mkvc_test_sycl_export_fd",
            I,
            P,
            P,
            ct.POINTER(I),
            ct.POINTER(ct.c_uint64),
            ct.POINTER(ct.c_uint64),
        )
        fd, size, offset = I(-1), ct.c_uint64(), ct.c_uint64()
        pointer = array.__sycl_usm_array_interface__["data"][0]
        check(
            export(
                array.sycl_queue.addressof_ref(),
                pointer,
                ct.byref(fd),
                ct.byref(size),
                ct.byref(offset),
            )
        )
        try:
            required = width * height * 3 // 2
            if size.value < offset.value + required or size.value > 0xFFFFFFFF:
                raise RuntimeError("USM allocation is not VA representable")
            prime = Prime()
            prime.fourcc, prime.width, prime.height = 0x3231564E, width, height
            prime.num_objects, prime.num_layers = 1, 1
            prime.objects[0].fd, prime.objects[0].size = fd.value, size.value
            prime.layers[0].format, prime.layers[0].planes = prime.fourcc, 2
            prime.layers[0].pitches[0] = prime.layers[0].pitches[1] = width
            prime.layers[0].offsets[0] = offset.value
            prime.layers[0].offsets[1] = offset.value + width * height
            attrs = (Attribute * 2)()
            attrs[0].type, attrs[0].flags, attrs[0].value.type = 6, 2, 1
            attrs[0].value.value.i = 0x40000000
            attrs[1].type, attrs[1].flags, attrs[1].value.type = 7, 2, 3
            attrs[1].value.value.p = ct.addressof(prime)
            create = bind(
                va,
                "vaCreateSurfaces",
                I,
                P,
                U,
                U,
                U,
                ct.POINTER(U),
                U,
                P,
                U,
            )
            check(
                create(
                    self.display,
                    1,
                    width,
                    height,
                    ct.byref(self.surface),
                    1,
                    attrs,
                    2,
                )
            )
        finally:
            if fd.value >= 0:
                os.close(fd.value)

    def close(self) -> None:
        """Destroy the imported VA surface while its display anchor is alive."""
        if getattr(self, "surface", U(0xFFFFFFFF)).value != 0xFFFFFFFF:
            check(self._destroy(self.display, ct.byref(self.surface), 1))
            self.surface.value = 0xFFFFFFFF

    def __del__(self) -> None:
        try:
            self.close()
        except Exception:
            pass


def reference_rgb(
    nv12: np.ndarray,
    width: int,
    height: int,
    color_space: str,
    color_range: str,
) -> np.ndarray:
    """Apply the documented nearest-chroma YUV matrix with uint8 rounding."""
    kr, kb = {
        "bt601": (0.2990, 0.1140),
        "bt709": (0.2126, 0.0722),
        "bt2020": (0.2627, 0.0593),
    }[color_space]
    kg = 1.0 - kr - kb
    if color_range == "limited":
        offset, multiplier, scale = 16.0, 255.0 / 219.0, 255.0 / 224.0
    else:
        offset, multiplier, scale = 0.0, 1.0, 1.0
    y = (nv12[:height, :width].astype(np.float32) - offset) * multiplier
    uv = nv12[height:, :width]
    u = np.repeat(np.repeat(uv[:, 0::2], 2, axis=0), 2, axis=1).astype(np.float32) - 128.0
    v = np.repeat(np.repeat(uv[:, 1::2], 2, axis=0), 2, axis=1).astype(np.float32) - 128.0
    values = (
        y + scale * 2.0 * (1.0 - kr) * v,
        y - scale * 2.0 * kb * (1.0 - kb) / kg * u - scale * 2.0 * kr * (1.0 - kr) / kg * v,
        y + scale * 2.0 * (1.0 - kb) * u,
    )
    return (
        np.stack([np.clip(value, 0.0, 255.0) + 0.5 for value in values], axis=-1)
        .astype(np.int32)
        .astype(np.uint8)
    )


try:
    library = ct.CDLL(helper)
    queue = dpctl.SyclQueue("level_zero:gpu:0")
    with mkvcodec.VideoCapture(fixture, backend="cpu", prefetch=0) as cpu_capture:
        cpu_frame = cpu_capture.read_i420()
    if cpu_frame is None:
        raise AssertionError("CPU reference decoder returned no frame")
    with mkvcodec.VideoCapture(
        fixture,
        backend="intel",
        prefetch=0,
        require_gpu_resident=True,
    ) as capture:
        source = capture.read_surface()
    if source is None:
        raise AssertionError("Intel decoder returned no frame")
    source.wait(5000)
    descriptor = source.descriptor
    width, height = int(descriptor["width"]), int(descriptor["height"])
    nv12_pitch = ((width + 63) // 64) * 64
    required_bytes = nv12_pitch * height * 3 // 2
    allocation_bytes = ((required_bytes + 2 * 1024**2 - 1) // (2 * 1024**2)) * (2 * 1024**2)
    allocation = ExportableAllocation(queue, library, allocation_bytes)
    memory = dpctl.memory.as_usm_memory(allocation)
    view = dpnp.tensor.usm_ndarray(
        (height * 3 // 2, width),
        dtype="u1",
        buffer=memory,
        strides=(nv12_pitch, 1),
    )
    nv12 = dpnp.asarray(view, copy=False)
    owner = UsmVaOwner(source, nv12, library, nv12_pitch, height)
    owner_ref = weakref.ref(owner)
    identity = copy_nv12(source, owner, width, height)
    materialized = dpnp.asnumpy(nv12)
    cpu_nv12 = np.empty_like(materialized)
    cpu_nv12[:height, :width] = cpu_frame.y
    cpu_nv12[height:, 0::2] = cpu_frame.u
    cpu_nv12[height:, 1::2] = cpu_frame.v
    nv12_difference = np.abs(materialized.astype(np.int16) - cpu_nv12.astype(np.int16))
    nv12_metrics = {
        "mean_abs": float(nv12_difference.mean()),
        "p99_abs": float(np.percentile(nv12_difference, 99)),
        "max_abs": int(nv12_difference.max()),
    }
    assert nv12_metrics["max_abs"] <= 1, nv12_metrics
    # The Intel OpenCL VA-sharing implementation rejects external RGBA VA
    # surfaces. Use the luma plane of a width*4 NV12 carrier as packed RGBA;
    # its unused chroma plane makes the required backing size 6 bytes/pixel.
    rgba_required_bytes = width * height * 6
    rgba_allocation_bytes = ((rgba_required_bytes + 2 * 1024**2 - 1) // (2 * 1024**2)) * (
        2 * 1024**2
    )
    rgba_allocation = ExportableAllocation(queue, library, rgba_allocation_bytes)
    rgba_memory = dpctl.memory.as_usm_memory(rgba_allocation)
    rgba_view = dpnp.tensor.usm_ndarray((height, width, 4), dtype="u1", buffer=rgba_memory)
    rgba = dpnp.asarray(rgba_view, copy=False)
    rgba_owner = UsmVaOwner(source, rgba, library, width * 4, height)
    fused_results = []
    fused_identity = None
    for fused_space in ("bt601", "bt709", "bt2020"):
        for fused_range in ("limited", "full"):
            _, _, coefficients = resolve_yuv_conversion(fused_space, fused_range, height)
            fused_identity = convert_nv12_rgba(
                source,
                rgba_owner,
                width,
                height,
                coefficients,
            )
            fused_actual = dpnp.asnumpy(rgba)[..., :3]
            fused_expected = reference_rgb(cpu_nv12, width, height, fused_space, fused_range)
            fused_difference = np.abs(
                fused_actual.astype(np.int16) - fused_expected.astype(np.int16)
            )
            fused_metrics = {
                "color_space": fused_space,
                "color_range": fused_range,
                "mean_abs": float(fused_difference.mean()),
                "p99_abs": float(np.percentile(fused_difference, 99)),
                "max_abs": int(fused_difference.max()),
                "channel_max_abs": [
                    int(fused_difference[..., channel].max()) for channel in range(3)
                ],
            }
            assert fused_metrics["max_abs"] <= 1, fused_metrics
            assert fused_metrics["p99_abs"] <= 1, fused_metrics
            fused_results.append(fused_metrics)
    _, _, fused_coefficients = resolve_yuv_conversion("bt709", "limited", height)
    pointer = int(nv12.__sycl_usm_array_interface__["data"][0])
    usm_frame = mkvcodec.GpuFrame.import_usm_nv12(
        pointer=pointer,
        context=queue.sycl_context.addressof_ref(),
        queue=queue.addressof_ref(),
        device_id=int(descriptor["device_id"]),
        frame_size=(width, height),
        pitch=nv12_pitch,
        owner=owner,
        pts_ns=int(descriptor["pts_ns"]),
        producer_synchronized=True,
    )
    processor = mkvcodec.GpuProcessor(backend="intel")
    results = []
    for color_space in ("bt601", "bt709", "bt2020"):
        for color_range in ("limited", "full"):
            image = processor.convert(
                usm_frame,
                format="rgb",
                layout="hwc",
                dtype="uint8",
                color_space=color_space,
                color_range=color_range,
            )
            image.wait(5000)
            actual = dpnp.asnumpy(dpnp.from_dlpack(image, copy=False))
            expected = reference_rgb(cpu_nv12, width, height, color_space, color_range)
            difference = np.abs(actual.astype(np.int16) - expected.astype(np.int16))
            metrics = {
                "color_space": color_space,
                "color_range": color_range,
                "mean_abs": float(difference.mean()),
                "p99_abs": float(np.percentile(difference, 99)),
                "max_abs": int(difference.max()),
                "channel_max_abs": [int(difference[..., channel].max()) for channel in range(3)],
            }
            assert metrics["max_abs"] <= 1, metrics
            assert metrics["p99_abs"] <= 1, metrics
            results.append(metrics)
            image.close()
    usm_frame.close()
    benchmark = None
    benchmark_frames = int(os.environ.get("MKVC_INTEL_VA_RGB_BENCH_FRAMES", "0"))
    if benchmark_frames:
        if benchmark_frames < 1:
            raise ValueError("MKVC_INTEL_VA_RGB_BENCH_FRAMES must be positive")
        materialize_seconds = 0.0
        rgb_seconds = 0.0
        fused_seconds = 0.0
        with OpenClReuseSession() as session:
            # Build and warm OpenCL/dpnp kernels outside the timed interval.
            copy_nv12(source, owner, width, height, session=session)
            warm_frame = mkvcodec.GpuFrame.import_usm_nv12(
                pointer=pointer,
                context=queue.sycl_context.addressof_ref(),
                queue=queue.addressof_ref(),
                device_id=int(descriptor["device_id"]),
                frame_size=(width, height),
                pitch=width,
                owner=owner,
                pts_ns=0,
                producer_synchronized=True,
            )
            warm_image = processor.convert(warm_frame)
            warm_image.wait(5000)
            warm_image.close()
            warm_frame.close()
            for index in range(benchmark_frames):
                started = time.perf_counter()
                copy_nv12(
                    source,
                    owner,
                    width,
                    height,
                    frame_index=index,
                    session=session,
                )
                materialize_seconds += time.perf_counter() - started
                started = time.perf_counter()
                benchmark_frame = mkvcodec.GpuFrame.import_usm_nv12(
                    pointer=pointer,
                    context=queue.sycl_context.addressof_ref(),
                    queue=queue.addressof_ref(),
                    device_id=int(descriptor["device_id"]),
                    frame_size=(width, height),
                    pitch=width,
                    owner=owner,
                    pts_ns=index * 33333333,
                    producer_synchronized=True,
                )
                benchmark_image = processor.convert(benchmark_frame)
                benchmark_image.wait(5000)
                benchmark_image.close()
                benchmark_frame.close()
                rgb_seconds += time.perf_counter() - started
        with OpenClReuseSession() as fused_session:
            convert_nv12_rgba(
                source,
                rgba_owner,
                width,
                height,
                fused_coefficients,
                session=fused_session,
            )
            for index in range(benchmark_frames):
                started = time.perf_counter()
                convert_nv12_rgba(
                    source,
                    rgba_owner,
                    width,
                    height,
                    fused_coefficients,
                    frame_index=index,
                    session=fused_session,
                )
                fused_seconds += time.perf_counter() - started
        decode_frames = 0
        started = time.perf_counter()
        with mkvcodec.VideoCapture(
            fixture,
            backend="intel",
            prefetch=0,
            require_gpu_resident=True,
        ) as benchmark_capture:
            while decode_frames < benchmark_frames:
                decoded = benchmark_capture.read_surface()
                if decoded is None:
                    break
                decoded.wait(5000)
                decoded.close()
                decode_frames += 1
        decode_seconds = time.perf_counter() - started
        if decode_frames != benchmark_frames:
            raise AssertionError(
                f"benchmark source ended after {decode_frames}/{benchmark_frames} frames"
            )
        full_serial_seconds = decode_seconds + materialize_seconds + rgb_seconds
        fused_full_serial_seconds = decode_seconds + fused_seconds
        benchmark = {
            "frames": benchmark_frames,
            "decode_frames": decode_frames,
            "decode_fps": decode_frames / decode_seconds,
            "materialize_fps": benchmark_frames / materialize_seconds,
            "rgb_fps": benchmark_frames / rgb_seconds,
            "serial_pipeline_fps": benchmark_frames / (materialize_seconds + rgb_seconds),
            "full_serial_fps": benchmark_frames / full_serial_seconds,
            "fused_va_rgb_fps": benchmark_frames / fused_seconds,
            "fused_full_serial_fps": benchmark_frames / fused_full_serial_seconds,
            "decode_seconds": decode_seconds,
            "materialize_seconds": materialize_seconds,
            "rgb_seconds": rgb_seconds,
            "full_serial_seconds": full_serial_seconds,
            "fused_va_rgb_seconds": fused_seconds,
            "fused_full_serial_seconds": fused_full_serial_seconds,
        }
    rgba_owner.close()
    owner.close()
    source.close()
    del usm_frame, source, owner, nv12, view, memory, allocation
    gc.collect()
    assert owner_ref() is None
except (OSError, RuntimeError, ValueError) as exception:
    if os.environ.get("MKVC_INTEL_VA_RGB_DEBUG") == "1":
        raise
    skip(str(exception))

print(
    json.dumps(
        {
            "device": identity,
            "fused_device": fused_identity,
            "fused_pixel_metrics": fused_results,
            "benchmark": benchmark,
            "nv12_materialization_metrics": nv12_metrics,
            "pixel_metrics": results,
        },
        sort_keys=True,
    )
)
