"""Decode -> external OpenCL inversion -> VA shared import -> VP9 encode."""
import gc
import os
import sys
import tempfile
import weakref
import faulthandler
import json
from pathlib import Path
import time

native_library, extension_dir, package_dir, fixture = sys.argv[1:5]
os.environ["MKVC_LIBRARY_PATH"] = native_library
sys.path[:0] = [package_dir, extension_dir]
import numpy as np
import _dlpack
import mkvcodec
import mkvcodec._api as api
from intel_va_opencl_support import Unsupported, VaOwner, invert_luma
api._dlpack = _dlpack
original_check = api.native.check


def checked(result):
    if result == 3:
        raise Unsupported(api.native.lib.mkvc_get_last_error().decode())
    original_check(result)


api.native.check = checked


def roundtrip(frames):
    """One bounded batch, including teardown and an independent CPU oracle."""
    assert VaOwner.live == 0
    VaOwner.peak = VaOwner.released = 0
    with mkvcodec.VideoCapture(fixture, backend="cpu", prefetch=0) as reference:
        reference_y = reference.read_i420().y.astype(np.float32)
    with mkvcodec.VideoCapture(fixture, backend="intel", prefetch=0,
                              require_gpu_resident=True) as capture:
        source = capture.read_surface()
        source.wait(5000)
        desc = source.descriptor
        width, height = desc["width"], desc["height"]
    source_ref = weakref.ref(source)
    with tempfile.TemporaryDirectory() as directory:
        path = os.path.join(directory, "opencl-inverted.webm")
        with mkvcodec.VideoWriter(path, backend="intel", fps=30,
                                 frame_size=(width, height), queue_size=0,
                                 require_gpu_resident=True) as writer:
            for index in range(frames):
                owner = VaOwner(source, width, height)
                invert_luma(source, owner, width, height)
                imported = mkvcodec.GpuFrame.import_va_surface(
                    display=owner.display, surface_id=owner.surface.value,
                    device_id=desc["device_id"], frame_size=(width, height),
                    pts_ns=index * 33333333, owner=owner, producer_synchronized=True)
                del owner
                assert imported.native_handle["handles"][0] == source.native_handle["handles"][0]
                assert imported.native_handle["handles"][1] != source.native_handle["handles"][1]
                writer.write_surface(imported)
                imported.close()
                del imported
                gc.collect()
                # Display anchor plus runtime-referenced imports; no unbounded owners.
                assert 1 <= VaOwner.live <= 65
            del source
            gc.collect()
            assert source_ref() is not None
            assert writer.metrics.copy_path == "zero_copy"  # Library boundary only.
        gc.collect()
        assert source_ref() is None
        assert VaOwner.live == 0 and VaOwner.released == frames
        assert VaOwner.peak <= 65
        count, previous_pts = 0, -1
        with mkvcodec.VideoCapture(path, backend="cpu", prefetch=0) as capture:
            while (frame := capture.read_i420()) is not None:
                difference = frame.y.astype(np.float32) - (255 - reference_y)
                assert float(np.mean(difference * difference)) < 205.63  # Y PSNR > 25 dB.
                assert frame.pts_ns > previous_pts
                previous_pts = frame.pts_ns
                count += 1
        assert count == frames
    print(f"External OpenCL inversion roundtrip: {frames} frames; owner peak={VaOwner.peak}; "
          "no host pixel transfer in producer; driver-internal copy path unqualified")
    return VaOwner.peak


def process_resources():
    """Current Linux process resources, not VRAM or peak-RSS substitutes."""
    return {
        "rss_bytes": int(Path("/proc/self/statm").read_text().split()[1]) * os.sysconf("SC_PAGE_SIZE"),
        "fds": len(os.listdir("/proc/self/fd")),
        "threads": len(os.listdir("/proc/self/task")),
    }


def main():
    """Optional same-process soak; each batch must release every external owner."""
    report_path = os.environ.get("MKVC_OPENCL_SOAK_REPORT")
    if report_path:
        Path(report_path).write_text('{"validation":"not_completed"}\n')
    frames = int(os.environ.get("MKVC_OPENCL_TEST_FRAMES", "32"))
    seconds = float(os.environ.get("MKVC_OPENCL_SOAK_SECONDS", "0"))
    assert 1 <= frames <= 10000 and 0 <= seconds <= 86400
    # Conservative engineering regression budgets, not approved performance SLAs.
    budgets = {"rss_bytes": 256 * 1024 * 1024, "fds": 2, "threads": 4}
    report = {"version": 1, "validation": "not_completed", "pid": os.getpid(),
              "requested_seconds": seconds, "frames_per_batch": frames, "batches": 0,
              "total_frames": 0, "owner_peak": 0, "growth_budgets": budgets,
              "scope": "post-close RSS/FD/thread samples; no VRAM or driver-copy proof"}

    def save():
        if report_path:
            Path(report_path).write_text(json.dumps(report, indent=2) + "\n")

    started = time.monotonic()
    save()
    try:
        while True:
            report["owner_peak"] = max(report["owner_peak"], roundtrip(frames))
            gc.collect()
            sample = process_resources()
            report["batches"] += 1
            report["total_frames"] += frames
            report["elapsed_seconds"] = time.monotonic() - started
            if report["batches"] == 1:
                report["baseline"] = sample.copy()  # Warm up runtime and Python caches first.
                report["high_water"] = sample.copy()
            report["last"] = sample
            for name, value in sample.items():
                report["high_water"][name] = max(report["high_water"][name], value)
                if value > report["baseline"][name] + budgets[name]:
                    raise AssertionError(f"Post-close {name} growth exceeded budget: {report}")
            save()  # Bounded-size evidence survives a later failure/timeout.
            if seconds == 0 or (report["batches"] >= 2 and report["elapsed_seconds"] >= seconds):
                break
        report["validation"] = "passed"
        save()
        print(json.dumps(report, sort_keys=True))
    except BaseException:
        report["validation"] = "failed"
        report["elapsed_seconds"] = time.monotonic() - started
        save()
        raise


if __name__ == "__main__":
    faulthandler.enable()
    try:
        main()
    except Unsupported as error:
        print(error)
        sys.exit(1 if os.environ.get("MKVC_REQUIRE_INTEL_EXTERNAL_IMPORT") else 77)
