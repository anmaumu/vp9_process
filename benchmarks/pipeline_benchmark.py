"""Reproducible end-to-end mkvcodec encode/decode benchmark.

The benchmark intentionally uses only the public Python API. Results are emitted
as a versioned JSON document suitable for checked-in or release-gate baselines.
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import statistics
import sys
import tempfile
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import numpy as np

import mkvcodec


def percentile(values: list[float], fraction: float) -> float:
    """Return a linearly interpolated percentile without third-party stats."""
    if not values:
        return 0.0
    ordered = sorted(values)
    position = (len(ordered) - 1) * fraction
    lower = int(position)
    upper = min(lower + 1, len(ordered) - 1)
    weight = position - lower
    return ordered[lower] * (1.0 - weight) + ordered[upper] * weight


def peak_rss_bytes() -> int | None:
    """Return process peak RSS where the standard library exposes it."""
    try:
        import resource

        value = int(resource.getrusage(resource.RUSAGE_SELF).ru_maxrss)
        return value if sys.platform == "darwin" else value * 1024
    except (ImportError, AttributeError, OSError):
        return None


def make_frame(width: int, height: int) -> np.ndarray:
    rows, columns = np.indices((height, width), dtype=np.uint32)
    frame = np.empty((height, width, 3), dtype=np.uint8)
    frame[..., 0] = columns & 0xFF
    frame[..., 1] = rows & 0xFF
    frame[..., 2] = (columns + rows) & 0xFF
    return frame


def run(args: argparse.Namespace) -> dict[str, Any]:
    """Run one encode/decode case and return a stable JSON-compatible record."""
    if args.width <= 0 or args.height <= 0 or args.width % 2 or args.height % 2:
        raise ValueError("width and height must be positive even values")
    if args.frames <= 0 or args.fps <= 0:
        raise ValueError("frames and fps must be positive")

    temporary: tempfile.TemporaryDirectory[str] | None = None
    if args.media_output:
        media_path = Path(args.media_output).resolve()
        media_path.parent.mkdir(parents=True, exist_ok=True)
    else:
        temporary = tempfile.TemporaryDirectory()
        media_path = Path(temporary.name) / "benchmark.webm"

    frame = make_frame(args.width, args.height)
    write_latencies: list[float] = []
    writer = mkvcodec.VideoWriter(
        media_path,
        codec=args.codec,
        backend=args.backend,
        fps=args.fps,
        frame_size=(args.width, args.height),
        quality=args.quality,
        queue_size=args.queue_size,
    )
    encode_started = time.perf_counter()
    try:
        for index in range(args.frames):
            frame[0, 0, 0] = index & 0xFF
            started = time.perf_counter()
            writer.write(frame)
            write_latencies.append(time.perf_counter() - started)
    finally:
        writer.close()
    encode_seconds = time.perf_counter() - encode_started

    decoded = 0
    first_frame_seconds: float | None = None
    decode_started = time.perf_counter()
    with mkvcodec.VideoCapture(
        media_path,
        codec=args.codec,
        backend=args.backend,
        prefetch=args.prefetch,
    ) as capture:
        for decoded_frame in capture:
            if first_frame_seconds is None:
                first_frame_seconds = time.perf_counter() - decode_started
            if decoded_frame.shape != (args.height, args.width, 3):
                raise RuntimeError("decoded frame dimensions changed")
            decoded += 1
    decode_seconds = time.perf_counter() - decode_started
    if decoded != args.frames:
        raise RuntimeError(f"decoded {decoded} frames, expected {args.frames}")

    result: dict[str, Any] = {
        "schema_version": 1,
        "recorded_at_utc": datetime.now(timezone.utc).isoformat(),
        "case": {
            "backend": args.backend,
            "codec": args.codec,
            "width": args.width,
            "height": args.height,
            "frames": args.frames,
            "fps_nominal": args.fps,
            "quality": args.quality,
            "queue_size": args.queue_size,
            "prefetch": args.prefetch,
            "input_pixel_format": "bgr24",
            "output_pixel_format": "bgr24",
        },
        "environment": {
            "system": platform.system(),
            "release": platform.release(),
            "machine": platform.machine(),
            "processor": platform.processor(),
            "python": platform.python_version(),
            "numpy": np.__version__,
        },
        "measurements": {
            "encode_total_seconds": encode_seconds,
            "encode_fps": args.frames / encode_seconds,
            "write_latency_ms_mean": statistics.fmean(write_latencies) * 1000,
            "write_latency_ms_p50": percentile(write_latencies, 0.50) * 1000,
            "write_latency_ms_p95": percentile(write_latencies, 0.95) * 1000,
            "write_latency_ms_max": max(write_latencies) * 1000,
            "decode_total_seconds": decode_seconds,
            "decode_fps": decoded / decode_seconds,
            "decode_first_frame_ms": (first_frame_seconds or 0.0) * 1000,
            "encoded_bytes": media_path.stat().st_size,
            "process_peak_rss_bytes": peak_rss_bytes(),
        },
        "observed_path": {
            "encode_input_copy": "python_numpy_to_native_owned_cpu",
            "decode_output_copy": "native_owned_i420_to_python_bgr",
            "zero_copy": False,
        },
    }
    if temporary is not None:
        temporary.cleanup()
    return result


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--backend", choices=("cpu", "intel"), default="cpu")
    parser.add_argument("--codec", choices=("vp9", "av1"), default="vp9")
    parser.add_argument("--width", type=int, default=1920)
    parser.add_argument("--height", type=int, default=1080)
    parser.add_argument("--frames", type=int, default=300)
    parser.add_argument("--fps", type=int, default=30)
    parser.add_argument("--quality", type=int, default=32)
    parser.add_argument("--queue-size", type=int, default=8)
    parser.add_argument("--prefetch", type=int, default=4)
    parser.add_argument("--output", help="JSON result path; stdout when omitted")
    parser.add_argument("--media-output", help="Retain the encoded WebM here")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    result = run(args)
    encoded = json.dumps(result, indent=2, sort_keys=True)
    if args.output:
        output = Path(args.output)
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(encoded + "\n", encoding="utf-8")
    else:
        print(encoded)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exception:
        print(f"benchmark failed: {exception}", file=sys.stderr)
        raise
