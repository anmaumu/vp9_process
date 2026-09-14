#!/usr/bin/env python3
"""Compare MKVCodec CPU VP9 decode with OpenCV's FFmpeg backend.

Both paths open the same file, decode every frame into an owned uint8 BGR
array, reach end-of-stream, and close the capture inside the timed interval.
The benchmark explicitly disables hardware acceleration and read-ahead and
sets the codec thread count for both implementations.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import platform
import statistics
import time
from pathlib import Path
from typing import Callable

import cv2
import mkvcodec
import numpy as np


def percentile(values: list[float], fraction: float) -> float:
    """Return a linearly interpolated percentile for a non-empty sample."""
    ordered = sorted(values)
    position = (len(ordered) - 1) * fraction
    lower = int(position)
    upper = min(lower + 1, len(ordered) - 1)
    weight = position - lower
    return ordered[lower] * (1.0 - weight) + ordered[upper] * weight


def frame_digest(frame: np.ndarray | None) -> str:
    """Hash one diagnostic frame outside the measured decode interval."""
    if frame is None:
        return ""
    return hashlib.sha256(memoryview(np.ascontiguousarray(frame))).hexdigest()


def validate_frame(frame: np.ndarray, width: int, height: int) -> None:
    """Fail when a backend does not return the requested owned BGR layout."""
    if frame.dtype != np.uint8 or frame.shape != (height, width, 3):
        raise RuntimeError(
            f"decoder returned {frame.dtype} {frame.shape}, "
            f"expected uint8 {(height, width, 3)}"
        )


def run_mkvcodec(path: Path, threads: int, conversion_threads: int,
                 width: int, height: int) -> dict[str, object]:
    """Measure one complete MKVCodec CPU read."""
    first: np.ndarray | None = None
    last: np.ndarray | None = None
    first_frame_seconds: float | None = None
    frames = 0
    cpu_started = time.process_time()
    started = time.perf_counter()
    with mkvcodec.VideoCapture(
        path,
        codec="vp9",
        backend="cpu",
        threads=threads,
        prefetch=0,
        conversion_threads=conversion_threads,
    ) as capture:
        while True:
            frame = capture.read()
            if frame is None:
                break
            validate_frame(frame, width, height)
            if first is None:
                first = frame
                first_frame_seconds = time.perf_counter() - started
            last = frame
            frames += 1
    elapsed = time.perf_counter() - started
    cpu_elapsed = time.process_time() - cpu_started
    if first is None or first_frame_seconds is None:
        raise RuntimeError("MKVCodec decoded no frames")
    return {
        "frames": frames,
        "seconds": elapsed,
        "fps": frames / elapsed,
        "first_frame_ms": first_frame_seconds * 1000,
        "cpu_seconds": cpu_elapsed,
        "cpu_to_wall_ratio": cpu_elapsed / elapsed,
        "reported_copy_path": capture.metrics.copy_path,
        "first_frame_sha256": frame_digest(first),
        "last_frame_sha256": frame_digest(last),
    }


def run_opencv(path: Path, threads: int, _conversion_threads: int,
               width: int, height: int) -> dict[str, object]:
    """Measure one complete OpenCV FFmpeg CPU read."""
    parameters = [
        cv2.CAP_PROP_HW_ACCELERATION,
        cv2.VIDEO_ACCELERATION_NONE,
        cv2.CAP_PROP_N_THREADS,
        threads,
    ]
    first: np.ndarray | None = None
    last: np.ndarray | None = None
    first_frame_seconds: float | None = None
    frames = 0
    cpu_started = time.process_time()
    started = time.perf_counter()
    capture = cv2.VideoCapture(str(path), cv2.CAP_FFMPEG, parameters)
    try:
        if not capture.isOpened():
            raise RuntimeError("OpenCV FFmpeg failed to open the input")
        if int(capture.get(cv2.CAP_PROP_BACKEND)) != cv2.CAP_FFMPEG:
            raise RuntimeError("OpenCV did not select the FFmpeg backend")
        reported_threads = int(capture.get(cv2.CAP_PROP_N_THREADS))
        if reported_threads != threads:
            raise RuntimeError(
                f"OpenCV reported {reported_threads} decoder threads; requested {threads}"
            )
        while True:
            ok, frame = capture.read()
            if not ok:
                break
            validate_frame(frame, width, height)
            if first is None:
                first = frame
                first_frame_seconds = time.perf_counter() - started
            last = frame
            frames += 1
    finally:
        capture.release()
    elapsed = time.perf_counter() - started
    cpu_elapsed = time.process_time() - cpu_started
    if first is None or first_frame_seconds is None:
        raise RuntimeError("OpenCV FFmpeg decoded no frames")
    return {
        "frames": frames,
        "seconds": elapsed,
        "fps": frames / elapsed,
        "first_frame_ms": first_frame_seconds * 1000,
        "cpu_seconds": cpu_elapsed,
        "cpu_to_wall_ratio": cpu_elapsed / elapsed,
        "reported_decoder_threads": reported_threads,
        "first_frame_sha256": frame_digest(first),
        "last_frame_sha256": frame_digest(last),
    }


def compare_representative_pixels(
    path: Path, width: int, height: int, frame_count: int
) -> dict[str, object]:
    """Compare three representative BGR frames outside timed measurements.

    Different YUV-to-BGR implementations can make byte hashes differ through
    rounding alone.  Mean absolute error, maximum absolute error and PSNR make
    that distinction visible without weakening the frame-count correctness gate.
    """
    indices = sorted({0, (frame_count - 1) // 2, frame_count - 1})
    wanted = set(indices)
    decoded: dict[str, dict[int, np.ndarray]] = {
        "mkvcodec": {},
        "opencv_ffmpeg": {},
    }
    with mkvcodec.VideoCapture(
        path,
        codec="vp9",
        backend="cpu",
        threads=1,
        prefetch=0,
        conversion_threads=1,
    ) as capture:
        index = 0
        while wanted - decoded["mkvcodec"].keys():
            frame = capture.read()
            if frame is None:
                break
            if index in wanted:
                validate_frame(frame, width, height)
                decoded["mkvcodec"][index] = frame
            index += 1

    parameters = [
        cv2.CAP_PROP_HW_ACCELERATION,
        cv2.VIDEO_ACCELERATION_NONE,
        cv2.CAP_PROP_N_THREADS,
        1,
    ]
    capture = cv2.VideoCapture(str(path), cv2.CAP_FFMPEG, parameters)
    try:
        if not capture.isOpened():
            raise RuntimeError("OpenCV FFmpeg failed to open the pixel-check input")
        index = 0
        while wanted - decoded["opencv_ffmpeg"].keys():
            ok, frame = capture.read()
            if not ok:
                break
            if index in wanted:
                validate_frame(frame, width, height)
                decoded["opencv_ffmpeg"][index] = frame
            index += 1
    finally:
        capture.release()

    missing = {
        name: sorted(wanted - frames.keys())
        for name, frames in decoded.items()
        if wanted - frames.keys()
    }
    if missing:
        raise RuntimeError(f"representative pixel frames are missing: {missing}")

    comparisons: list[dict[str, object]] = []
    for index in indices:
        left = decoded["mkvcodec"][index]
        right = decoded["opencv_ffmpeg"][index]
        difference = np.abs(left.astype(np.int16) - right.astype(np.int16))
        squared = difference.astype(np.float64) ** 2
        mse = float(np.mean(squared))
        psnr = None if mse == 0 else 10.0 * math.log10(255.0**2 / mse)
        comparisons.append({
            "frame_index": index,
            "mean_absolute_error": float(np.mean(difference)),
            "maximum_absolute_error": int(np.max(difference)),
            "exact_component_fraction": float(np.mean(difference == 0)),
            "psnr_db": psnr,
            "channel_mean_absolute_error_bgr": [
                float(value) for value in np.mean(difference, axis=(0, 1))
            ],
        })
    return {
        "method": "owned BGR comparison at first, middle, and last frame",
        "threads": 1,
        "frames": comparisons,
        "mean_absolute_error_average": statistics.mean(
            float(item["mean_absolute_error"]) for item in comparisons
        ),
        "maximum_absolute_error": max(
            int(item["maximum_absolute_error"]) for item in comparisons
        ),
        "psnr_db_average": (
            statistics.mean(
                float(item["psnr_db"])
                for item in comparisons
                if item["psnr_db"] is not None
            )
            if any(item["psnr_db"] is not None for item in comparisons)
            else None
        ),
    }


def summarize(samples: list[dict[str, object]]) -> dict[str, float]:
    """Aggregate repeated complete-file measurements."""
    fps = [float(sample["fps"]) for sample in samples]
    elapsed = [float(sample["seconds"]) for sample in samples]
    first = [float(sample["first_frame_ms"]) for sample in samples]
    cpu_ratio = [float(sample["cpu_to_wall_ratio"]) for sample in samples]
    return {
        "fps_median": statistics.median(fps),
        "fps_min": min(fps),
        "fps_max": max(fps),
        "seconds_median": statistics.median(elapsed),
        "first_frame_ms_median": statistics.median(first),
        "first_frame_ms_p95": percentile(first, 0.95),
        "cpu_to_wall_ratio_median": statistics.median(cpu_ratio),
    }


def run(args: argparse.Namespace) -> dict[str, object]:
    """Run warm-ups and alternating measured passes for every thread count."""
    path = args.input.resolve()
    info = mkvcodec.probe_video(path)
    if info.codec != "vp9":
        raise ValueError("comparison input must contain VP9")
    if info.frame_count < 1:
        raise ValueError("comparison input must contain at least one frame")
    cv2.setNumThreads(1)
    runners: dict[str, Callable[..., dict[str, object]]] = {
        "mkvcodec": run_mkvcodec,
        "opencv_ffmpeg": run_opencv,
    }
    comparisons: list[dict[str, object]] = []
    for threads in args.threads:
        if threads < 1:
            raise ValueError("every thread count must be positive")
        for _ in range(args.warmup_runs):
            for runner in runners.values():
                runner(path, threads, args.conversion_threads, info.width, info.height)
        samples: dict[str, list[dict[str, object]]] = {name: [] for name in runners}
        for iteration in range(args.runs):
            order = tuple(runners) if iteration % 2 == 0 else tuple(reversed(runners))
            for name in order:
                result = runners[name](
                    path, threads, args.conversion_threads, info.width, info.height
                )
                if int(result["frames"]) != info.frame_count:
                    raise RuntimeError(
                        f"{name} decoded {result['frames']} frames; expected {info.frame_count}"
                    )
                samples[name].append(result)
        summaries = {name: summarize(values) for name, values in samples.items()}
        comparisons.append({
            "threads": threads,
            "mkvcodec": summaries["mkvcodec"],
            "opencv_ffmpeg": summaries["opencv_ffmpeg"],
            "mkvcodec_over_opencv_fps": (
                summaries["mkvcodec"]["fps_median"]
                / summaries["opencv_ffmpeg"]["fps_median"]
            ),
            "raw_samples": samples,
        })
    build_lines = [
        line.strip()
        for line in cv2.getBuildInformation().splitlines()
        if "FFMPEG:" in line or "avcodec:" in line or "Parallel framework:" in line
    ]
    result: dict[str, object] = {
        "schema_version": 1,
        "case": {
            "input": str(path),
            "input_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            "codec": "vp9",
            "output_format": "bgr24_owned",
            "width": info.width,
            "height": info.height,
            "frames": info.frame_count,
            "fps_nominal": info.fps,
            "include_open_eos_close": True,
            "hardware_acceleration": False,
            "prefetch": 0,
            "mkvcodec_conversion_threads": args.conversion_threads,
            "warmup_runs": args.warmup_runs,
            "measured_runs": args.runs,
            "alternating_order": True,
        },
        "environment": {
            "system": platform.system(),
            "release": platform.release(),
            "machine": platform.machine(),
            "processor": platform.processor(),
            "python": platform.python_version(),
            "numpy": np.__version__,
            "mkvcodec": mkvcodec.__version__,
            "opencv": cv2.__version__,
            "opencv_global_threads": cv2.getNumThreads(),
            "opencv_build": build_lines,
        },
        "comparisons": comparisons,
        "interpretation": {
            "scope": "Python API complete-file CPU VP9 decode to owned BGR",
            "higher_fps_is_better": True,
            "cpu_to_wall_ratio_is_not_core_count_normalized": True,
            "pixel_hashes_are_diagnostic_not_a_quality_equivalence_gate": True,
        },
    }
    if not args.skip_pixel_check:
        result["representative_pixel_comparison"] = compare_representative_pixels(
            path, info.width, info.height, info.frame_count
        )
    return result


def main() -> None:
    """Parse command-line arguments and write one versioned JSON report."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--threads", type=int, nargs="+", default=[1, 16])
    parser.add_argument("--conversion-threads", type=int, default=1)
    parser.add_argument("--warmup-runs", type=int, default=1)
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument(
        "--skip-pixel-check",
        action="store_true",
        help="skip the untimed representative-frame BGR comparison",
    )
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.runs < 1 or args.warmup_runs < 0:
        parser.error("runs must be positive and warmup-runs must be non-negative")
    if args.conversion_threads < 1:
        parser.error("conversion-threads must be positive")
    result = run(args)
    encoded = json.dumps(result, indent=2, sort_keys=True)
    if args.output is None:
        print(encoded)
    else:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded + "\n", encoding="utf-8")
        print(args.output)


if __name__ == "__main__":
    main()
