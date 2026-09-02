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
from media_oracle import run_oracle
from contextlib import nullcontext

native_library, extension_dir, package_dir, fixture = sys.argv[1:5]
os.environ["MKVC_LIBRARY_PATH"] = native_library
sys.path[:0] = [package_dir, extension_dir]
import numpy as np
import _dlpack
import mkvcodec
import mkvcodec._api as api
from intel_va_opencl_support import Unsupported, VaOwner, invert_luma, OpenClReuseSession, reuse_program_enabled
from gpu_resource_monitor import ResourceMonitor
from gpu_trace_journal import journal
api._dlpack = _dlpack
original_check = api.native.check


def checked(result):
    if result == 3:
        raise Unsupported(api.native.lib.mkvc_get_last_error().decode())
    original_check(result)


api.native.check = checked


def roundtrip(frames, monitor=None):
    """One bounded batch, including teardown and an independent CPU oracle."""
    assert VaOwner.live == 0
    VaOwner.peak = VaOwner.released = 0
    journal.mark("cpu_reference")
    with mkvcodec.VideoCapture(fixture, backend="cpu", prefetch=0) as reference:
        reference_y = reference.read_i420().y.astype(np.float32)
    journal.mark("gpu_decode")
    with mkvcodec.VideoCapture(fixture, backend="intel", prefetch=0,
                              require_gpu_resident=True) as capture:
        source = capture.read_surface()
        source.wait(5000)
        desc = source.descriptor
        width, height = desc["width"], desc["height"]
    journal.surface("decoded", *source.native_handle["handles"][:2])
    journal.mark("encoder_open")
    source_ref = weakref.ref(source)
    with tempfile.TemporaryDirectory() as directory:
        path = os.path.join(directory, "opencl-inverted.webm")
        codec = os.environ.get("MKVC_OPENCL_OUTPUT_CODEC", "vp9")
        if codec not in ("vp9", "av1"):
            raise ValueError("Unsupported qualification codec")
        reuse = reuse_program_enabled()
        with mkvcodec.VideoWriter(path, backend="intel", codec=codec, fps=30,
                                 frame_size=(width, height), queue_size=0,
                                 require_gpu_resident=True) as writer, \
                (OpenClReuseSession() if reuse else nullcontext()) as session:
            for index in range(frames):
                journal.mark("external_allocate", frame=index)
                owner = VaOwner(source, width, height)
                journal.surface("processed", owner.display, owner.surface.value, index)
                journal.mark("external_opencl", frame=index)
                identity = invert_luma(source, owner, width, height, frame_index=index, session=session)
                if monitor:
                    monitor.set_processing_device(identity)
                journal.mark("encoder_import", frame=index)
                imported = mkvcodec.GpuFrame.import_va_surface(
                    display=owner.display, surface_id=owner.surface.value,
                    device_id=desc["device_id"], frame_size=(width, height),
                    pts_ns=index * 33333333, owner=owner, producer_synchronized=True)
                del owner
                assert imported.native_handle["handles"][0] == source.native_handle["handles"][0]
                assert imported.native_handle["handles"][1] != source.native_handle["handles"][1]
                journal.mark("encoder_submit", frame=index)
                writer.write_surface(imported)
                if monitor and index % 32 == 0:
                    monitor.sample("active")
                journal.mark("frame_release", frame=index)
                imported.close()
                del imported
                gc.collect()
                # Display anchor plus runtime-referenced imports; no unbounded owners.
                assert 1 <= VaOwner.live <= 65
            if session is not None:
                assert session.builds == 1 and session.completed_calls == frames
            del source
            gc.collect()
            assert source_ref() is not None
            assert writer.metrics.copy_path == "zero_copy"  # Library boundary only.
            journal.mark("encoder_flush_close")
        gc.collect()
        assert source_ref() is None
        assert VaOwner.live == 0 and VaOwner.released == frames
        assert VaOwner.peak <= 65
        journal.mark("cpu_output_oracle")
        count, previous_pts = 0, -1
        if codec == "av1":
            raw = run_oracle(["ffmpeg", "-v", "error", "-hwaccel", "none", "-i", path,
                              "-fps_mode", "passthrough", "-f", "rawvideo", "-pix_fmt", "yuv420p", "pipe:1"]).stdout
            pixels = np.frombuffer(raw, dtype=np.uint8).reshape(frames, height * 3 // 2, width)
            difference = pixels[:, :height].astype(np.float32) - (255 - reference_y)
            assert np.all(np.mean(difference * difference, axis=(1, 2)) < 205.63)
            probe = run_oracle(["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_frames",
                                "-show_entries", "frame=best_effort_timestamp_time", "-of", "json", path])
            timestamps = [float(f["best_effort_timestamp_time"]) for f in json.loads(probe.stdout)["frames"]]
            assert len(timestamps) == frames and all(b > a for a, b in zip(timestamps, timestamps[1:])), timestamps
            count = frames
        else:
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
    reuse = reuse_program_enabled()
    seconds = float(os.environ.get("MKVC_OPENCL_SOAK_SECONDS", "0"))
    assert 1 <= frames <= 10000 and 0 <= seconds <= 86400
    # Conservative engineering regression budgets, not approved performance SLAs.
    budgets = {"rss_bytes": 256 * 1024 * 1024, "fds": 2, "threads": 4}
    monitor = ResourceMonitor(required=os.environ.get("MKVC_REQUIRE_VRAM_OBSERVATION") == "1")
    report = {"version": 1, "validation": "not_completed", "pid": os.getpid(),
              "requested_seconds": seconds, "frames_per_batch": frames, "batches": 0,
              "total_frames": 0, "owner_peak": 0, "growth_budgets": budgets,
              "output_codec": os.environ.get("MKVC_OPENCL_OUTPUT_CODEC", "vp9"),
              "opencl_reuse_program": reuse,
              "scope": "sampled DRM memory and post-close RSS/FD/threads; no driver-copy proof"}
    report["gpu_memory"] = monitor.report

    def save():
        if report_path:
            Path(report_path).write_text(json.dumps(report, indent=2) + "\n")

    started = time.monotonic()
    journal.mark("run_start", frames=frames, fixture=fixture)
    save()
    try:
        while True:
            report["owner_peak"] = max(report["owner_peak"], roundtrip(frames, monitor))
            gc.collect()
            monitor.sample("post_close")
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
        journal.mark("run_complete", validation="passed")
        save()
        print(json.dumps(report, sort_keys=True))
    except BaseException:
        journal.mark("run_failed")
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
