"""Benchmark strict GPU-resident decode-surface to encode submission."""

from __future__ import annotations

import argparse
import json
import platform
import statistics
import time
from dataclasses import asdict
from pathlib import Path

import mkvcodec


def percentile(values: list[float], fraction: float) -> float:
    ordered = sorted(values)
    position = (len(ordered) - 1) * fraction
    lower = int(position)
    upper = min(lower + 1, len(ordered) - 1)
    weight = position - lower
    return ordered[lower] * (1.0 - weight) + ordered[upper] * weight


def run(args: argparse.Namespace) -> dict[str, object]:
    output = Path(args.media_output).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.unlink(missing_ok=True)
    latencies: list[float] = []
    frames = 0
    started = time.perf_counter()
    with mkvcodec.VideoCapture(
        args.input, codec=args.codec, backend="intel", prefetch=0,
        require_gpu_resident=True,
    ) as capture:
        surface = capture.read_surface()
        if surface is None:
            raise RuntimeError("input contains no frames")
        descriptor = surface.descriptor
        with mkvcodec.VideoWriter(
            output, codec=args.codec, backend="intel", fps=args.fps,
            frame_size=(descriptor["width"], descriptor["height"]),
            quality=args.quality, queue_size=0, require_gpu_resident=True,
        ) as writer:
            while surface is not None and (args.frames == 0 or frames < args.frames):
                submitted = time.perf_counter()
                writer.write_surface(surface)
                latencies.append(time.perf_counter() - submitted)
                surface.close()
                frames += 1
                surface = capture.read_surface()
            if surface is not None:
                surface.close()
        writer_metrics = writer.metrics
    elapsed = time.perf_counter() - started
    capture_metrics = capture.metrics
    if writer_metrics.copy_path != "zero_copy" or capture_metrics.copy_path != "zero_copy":
        raise RuntimeError("backend reported a non-GPU-resident copy path")
    return {
        "schema_version": 1,
        "case": {
            "backend": "intel",
            "codec": args.codec,
            "width": descriptor["width"],
            "height": descriptor["height"],
            "frames": frames,
            "fps_nominal": args.fps,
            "quality": args.quality,
            "require_gpu_resident": True,
        },
        "environment": {
            "system": platform.system(),
            "release": platform.release(),
            "machine": platform.machine(),
            "python": platform.python_version(),
        },
        "measurements": {
            "transcode_total_seconds": elapsed,
            "transcode_fps": frames / elapsed,
            "submit_latency_ms_mean": statistics.fmean(latencies) * 1000,
            "submit_latency_ms_p50": percentile(latencies, 0.50) * 1000,
            "submit_latency_ms_p95": percentile(latencies, 0.95) * 1000,
            "submit_latency_ms_max": max(latencies) * 1000,
            "encoded_bytes": output.stat().st_size,
        },
        "observed_path": {
            "decode_output": "onevpl_video_memory_surface",
            "encode_input": "shared_onevpl_surface",
            "reported_copy_path": "zero_copy",
            "trace_verified_cpu_transfer": False,
            "host_synchronous": True,
        },
        "native_metrics": {
            "encoder": asdict(writer_metrics),
            "decoder": asdict(capture_metrics),
        },
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True)
    parser.add_argument("--media-output", required=True)
    parser.add_argument("--codec", choices=("vp9", "av1"), default="vp9")
    parser.add_argument("--frames", type=int, default=0,
                        help="zero processes the complete input")
    parser.add_argument("--fps", type=int, default=30)
    parser.add_argument("--quality", type=int, default=32)
    parser.add_argument("--output", help="JSON result path; stdout when omitted")
    args = parser.parse_args()
    result = run(args)
    encoded = json.dumps(result, indent=2, sort_keys=True)
    if args.output:
        Path(args.output).write_text(encoded + "\n", encoding="utf-8")
    else:
        print(encoded)


if __name__ == "__main__":
    main()
