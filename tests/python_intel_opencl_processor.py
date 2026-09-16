"""Qualify the product Intel OpenCL processor, pool, and DLPack leases."""

from __future__ import annotations

import gc
import json
import os
import sys
import time
from collections import deque
from pathlib import Path

import numpy as np


def skip(message: str) -> None:
    print(f"Intel OpenCL processor test skipped: {message}")
    raise SystemExit(77)


try:
    import dpnp
except Exception as exception:
    skip(f"dpnp unavailable ({exception})")

native, extension, package, fixture, bridge = sys.argv[1:6]
os.environ["MKVC_LIBRARY_PATH"] = native
os.environ["MKVC_INTEL_SYCL_BRIDGE_PATH"] = bridge
sys.path[:0] = [package, extension]

import _dlpack  # noqa: E402
import mkvcodec  # noqa: E402
import mkvcodec.interop.dlpack as dlpack_api  # noqa: E402
from mkvcodec.interop.intel_opencl_processor import (  # noqa: E402
    IntelOpenClProcessorAdapter,
)
from gpu_resource_monitor import ResourceMonitor  # noqa: E402

dlpack_api.extension = _dlpack
qualification_started = False


def process_resources() -> dict[str, int]:
    """Return process resources used by the same-process soak growth gate."""
    return {
        "rss_bytes": int(Path("/proc/self/statm").read_text().split()[1])
        * os.sysconf("SC_PAGE_SIZE"),
        "fds": len(os.listdir("/proc/self/fd")),
        "threads": len(os.listdir("/proc/self/task")),
    }


def run_soak(seconds: float, frames_per_batch: int, report_path: Path) -> dict:
    """Repeat product conversions while bounding host and DRM resource growth."""
    budgets = {"rss_bytes": 256 * 1024**2, "fds": 2, "threads": 4}
    monitor = ResourceMonitor(required=os.environ.get("MKVC_REQUIRE_VRAM_OBSERVATION") == "1")
    pci = os.environ.get("MKVC_TEST_GPU_PCI")
    if pci:
        monitor.set_processing_device({"pci": pci, "name": "Intel OpenCL adapter"})
    report = {
        "version": 1,
        "validation": "not_completed",
        "requested_seconds": seconds,
        "frames_per_batch": frames_per_batch,
        "batches": 0,
        "frames": 0,
        "growth_budgets": budgets,
        "gpu_memory": monitor.report,
    }

    def save() -> None:
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(json.dumps(report, indent=2) + "\n")

    started = time.monotonic()
    save()
    try:
        while True:
            pending = deque()
            converted = 0
            with (
                mkvcodec.VideoCapture(
                    fixture,
                    backend="intel",
                    prefetch=0,
                    require_gpu_resident=True,
                ) as soak_capture,
                mkvcodec.GpuProcessor(
                    backend="intel",
                    adapters=(
                        IntelOpenClProcessorAdapter(
                            pool_size=3,
                            acquire_timeout_ms=5000,
                            bridge_path=bridge,
                        ),
                    ),
                ) as soak_processor,
            ):
                while converted < frames_per_batch:
                    frame = soak_capture.read_surface()
                    if frame is None:
                        break
                    output = soak_processor.convert(frame, format="rgb")
                    frame.close()
                    consumer = dpnp.from_dlpack(output, copy=False)
                    output.close()
                    pending.append(consumer)
                    converted += 1
                    if len(pending) == 3:
                        oldest = pending.popleft()
                        del oldest
                if converted == 0:
                    raise AssertionError("soak fixture produced no frames")
                monitor.sample("active")
                pending.clear()
                del consumer, output, frame
                gc.collect()
            gc.collect()
            monitor.sample("post_close")
            sample = process_resources()
            report["batches"] += 1
            report["frames"] += converted
            report["elapsed_seconds"] = time.monotonic() - started
            if report["batches"] == 1:
                report["baseline"] = sample.copy()
                report["high_water"] = sample.copy()
            report["last"] = sample
            for name, value in sample.items():
                report["high_water"][name] = max(report["high_water"][name], value)
                if value > report["baseline"][name] + budgets[name]:
                    raise AssertionError(f"post-close {name} growth exceeded budget: {report}")
            save()
            if report["batches"] >= 2 and report["elapsed_seconds"] >= seconds:
                break
        report["validation"] = "passed"
        save()
        return report
    except BaseException:
        report["validation"] = "failed"
        report["elapsed_seconds"] = time.monotonic() - started
        save()
        raise


def reference_rgb(nv12: np.ndarray, width: int, height: int) -> np.ndarray:
    """Return the documented BT.709 limited-range nearest-chroma oracle."""
    kr, kb = 0.2126, 0.0722
    kg = 1.0 - kr - kb
    y = (nv12[:height, :width].astype(np.float32) - 16.0) * (255.0 / 219.0)
    uv = nv12[height:, :width]
    u = np.repeat(np.repeat(uv[:, 0::2], 2, axis=0), 2, axis=1).astype(np.float32) - 128.0
    v = np.repeat(np.repeat(uv[:, 1::2], 2, axis=0), 2, axis=1).astype(np.float32) - 128.0
    scale = 255.0 / 224.0
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
    with mkvcodec.VideoCapture(fixture, backend="cpu", prefetch=0) as cpu_capture:
        cpu = cpu_capture.read_i420()
    if cpu is None:
        raise AssertionError("CPU decoder returned no frame")
    capture = mkvcodec.VideoCapture(
        fixture,
        backend="intel",
        prefetch=0,
        require_gpu_resident=True,
    )
    source = capture.read_surface()
    if source is None:
        raise AssertionError("Intel decoder returned no frame")
    descriptor = source.descriptor
    width, height = int(descriptor["width"]), int(descriptor["height"])
    nv12 = np.empty((height * 3 // 2, width), dtype=np.uint8)
    nv12[:height] = cpu.y
    nv12[height:, 0::2] = cpu.u
    nv12[height:, 1::2] = cpu.v
    adapter = IntelOpenClProcessorAdapter(
        pool_size=1,
        acquire_timeout_ms=0,
        bridge_path=bridge,
    )
    processor = mkvcodec.GpuProcessor(backend="intel", adapters=(adapter,))
    qualification_started = True
    image = processor.convert(
        source,
        format="rgb",
        color_space="bt709",
        color_range="limited",
    )
    assert image.info.adapter == "intel-opencl"
    assert image.info.completion == "opencl_event"
    consumer = dpnp.from_dlpack(image, copy=False)
    image.close()
    try:
        processor.convert(source, format="rgb")
    except RuntimeError as exception:
        assert "pool is exhausted" in str(exception)
    else:
        raise AssertionError("DLPack consumer did not retain output pool slot")
    actual = dpnp.asnumpy(consumer)
    expected = reference_rgb(nv12, width, height)
    difference = np.abs(actual.astype(np.int16) - expected.astype(np.int16))
    metrics = {
        "mean_abs": float(difference.mean()),
        "p99_abs": float(np.percentile(difference, 99)),
        "max_abs": int(difference.max()),
        "channel_max_abs": [int(difference[..., channel].max()) for channel in range(3)],
    }
    assert metrics["p99_abs"] <= 1, metrics
    assert metrics["max_abs"] <= 1, metrics
    del consumer
    gc.collect()
    format_metrics = {"rgb": metrics}
    for output_format in ("bgr", "rgba", "bgra"):
        reused = processor.convert(
            source,
            format=output_format,
            color_space="bt709",
            color_range="limited",
        )
        tensor = dpnp.from_dlpack(reused, copy=False)
        packed = dpnp.asnumpy(tensor)
        expected_packed = expected[..., ::-1] if output_format.startswith("bgr") else expected
        if output_format.endswith("a"):
            expected_packed = np.concatenate(
                (
                    expected_packed,
                    np.full((height, width, 1), 255, dtype=np.uint8),
                ),
                axis=-1,
            )
        packed_difference = np.abs(packed.astype(np.int16) - expected_packed.astype(np.int16))
        format_metrics[output_format] = {
            "p99_abs": float(np.percentile(packed_difference, 99)),
            "max_abs": int(packed_difference.max()),
        }
        assert format_metrics[output_format]["p99_abs"] <= 1, format_metrics
        assert format_metrics[output_format]["max_abs"] <= 1, format_metrics
        reused.close()
        del tensor
        gc.collect()
    processor.close()
    source.close()
    capture.close()
    benchmark = None
    benchmark_frames = int(os.environ.get("MKVC_INTEL_OPENCL_BENCH_FRAMES", "0"))
    if benchmark_frames:
        benchmark_capture = mkvcodec.VideoCapture(
            fixture,
            backend="intel",
            prefetch=0,
            require_gpu_resident=True,
        )
        benchmark_processor = mkvcodec.GpuProcessor(
            backend="intel",
            adapters=(
                IntelOpenClProcessorAdapter(
                    pool_size=3,
                    acquire_timeout_ms=5000,
                    bridge_path=bridge,
                ),
            ),
        )
        warm_source = benchmark_capture.read_surface()
        if warm_source is None:
            raise AssertionError("benchmark source has no warm-up frame")
        warm_image = benchmark_processor.convert(warm_source, format="rgb")
        warm_source.close()
        warm_image.wait(5000)
        warm_image.close()
        pending = deque()
        completed = 0
        started = time.perf_counter()
        while completed + len(pending) < benchmark_frames:
            frame = benchmark_capture.read_surface()
            if frame is None:
                break
            output = benchmark_processor.convert(frame, format="rgb")
            frame.close()
            pending.append(output)
            if len(pending) == 3:
                oldest = pending.popleft()
                oldest.wait(5000)
                oldest.close()
                completed += 1
        while pending:
            output = pending.popleft()
            output.wait(5000)
            output.close()
            completed += 1
        elapsed = time.perf_counter() - started
        if completed != benchmark_frames:
            raise AssertionError(
                f"benchmark source ended after {completed}/{benchmark_frames} frames"
            )
        benchmark = {
            "frames": completed,
            "seconds": elapsed,
            "fps": completed / elapsed,
            "pool_size": 3,
        }
        benchmark_processor.close()
        benchmark_capture.close()
    soak = None
    soak_seconds = float(os.environ.get("MKVC_INTEL_OPENCL_SOAK_SECONDS", "0"))
    if soak_seconds:
        if not 1 <= soak_seconds <= 86400:
            raise ValueError("MKVC_INTEL_OPENCL_SOAK_SECONDS must be in [1, 86400]")
        soak_frames = int(os.environ.get("MKVC_INTEL_OPENCL_SOAK_FRAMES", "100"))
        if not 1 <= soak_frames <= 10000:
            raise ValueError("MKVC_INTEL_OPENCL_SOAK_FRAMES must be in [1, 10000]")
        soak_report = Path(
            os.environ.get(
                "MKVC_INTEL_OPENCL_SOAK_REPORT",
                "intel_opencl_processor_soak.json",
            )
        )
        soak = run_soak(soak_seconds, soak_frames, soak_report)
except (ImportError, OSError, RuntimeError, ValueError) as exception:
    if qualification_started:
        raise
    skip(str(exception))

print(
    json.dumps(
        {
            "benchmark": benchmark,
            "format_metrics": format_metrics,
            "pixel_metrics": metrics,
            "pool_reuse": "passed",
            "soak": soak,
        },
        sort_keys=True,
    )
)
