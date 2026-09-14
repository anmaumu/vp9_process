#!/usr/bin/env python3
"""Fail a release check when a benchmark regresses from an approved baseline."""

from __future__ import annotations

import argparse
import json
import math
import pathlib
import sys


class BaselineError(RuntimeError):
    """Benchmark records cannot be compared or exceed their allowed regression."""


PIPELINE_CASE_KEYS = (
    "backend", "codec", "width", "height", "frames", "fps_nominal",
    "quality", "queue_size", "prefetch", "input_pixel_format",
    "output_pixel_format",
)
GPU_TRANSCODE_CASE_KEYS = (
    "backend", "input_codec", "output_codec", "width", "height", "frames", "fps_nominal",
    "quality", "require_gpu_resident", "input_sha256",
)
PIPELINE_HIGHER_IS_BETTER = ("encode_fps", "decode_fps")
PIPELINE_LOWER_IS_BETTER = (
    "write_latency_ms_p95", "decode_first_frame_ms",
    "process_peak_rss_bytes",
)
GPU_TRANSCODE_HIGHER_IS_BETTER = ("transcode_fps",)
GPU_TRANSCODE_LOWER_IS_BETTER = (
    "submit_latency_ms_p95", "process_peak_rss_bytes",
)


def _positive_measurement(record: dict, name: str) -> float:
    try:
        value = float(record["measurements"][name])
    except (KeyError, TypeError, ValueError) as error:
        raise BaselineError(f"missing numeric measurement: {name}") from error
    if not math.isfinite(value) or value <= 0:
        raise BaselineError(f"measurement must be finite and positive: {name}")
    return value


def compare(baseline: dict, candidate: dict) -> list[str]:
    """Compare one benchmark candidate with its approved baseline.

    Parameters
    ----------
    baseline, candidate:
        Parsed ``pipeline_benchmark.py`` JSON records. The baseline must contain
        ``performance_gate.max_regression_fraction``.

    Returns
    -------
    list of str
        Human-readable comparison observations.

    Raises
    ------
    BaselineError
        If records are incompatible, incomplete, or outside the threshold.
    """
    if baseline.get("schema_version") != 1 or candidate.get("schema_version") != 1:
        raise BaselineError("benchmark schema_version must be 1")
    gpu_transcode = baseline.get("case", {}).get("require_gpu_resident") is True
    if gpu_transcode != (candidate.get("case", {}).get("require_gpu_resident") is True):
        raise BaselineError("benchmark profile mismatch")
    if gpu_transcode:
        case_keys = GPU_TRANSCODE_CASE_KEYS
        higher_is_better = GPU_TRANSCODE_HIGHER_IS_BETTER
        lower_is_better = GPU_TRANSCODE_LOWER_IS_BETTER
    else:
        case_keys = PIPELINE_CASE_KEYS
        higher_is_better = PIPELINE_HIGHER_IS_BETTER
        lower_is_better = PIPELINE_LOWER_IS_BETTER
    for key in case_keys:
        if baseline.get("case", {}).get(key) != candidate.get("case", {}).get(key):
            raise BaselineError(f"benchmark case mismatch: {key}")
    try:
        fraction = float(baseline["performance_gate"]["max_regression_fraction"])
    except (KeyError, TypeError, ValueError) as error:
        raise BaselineError("baseline has no numeric regression threshold") from error
    if not math.isfinite(fraction) or not 0 <= fraction < 1:
        raise BaselineError("max_regression_fraction must be in [0, 1)")
    overrides = baseline["performance_gate"].get("metric_regression_fractions", {})
    if not isinstance(overrides, dict) or any(
        name not in higher_is_better + lower_is_better for name in overrides
    ):
        raise BaselineError("metric_regression_fractions contains an unknown metric")
    for name, value in overrides.items():
        try:
            metric_fraction = float(value)
        except (TypeError, ValueError) as error:
            raise BaselineError(f"invalid metric regression threshold: {name}") from error
        if not math.isfinite(metric_fraction) or not 0 <= metric_fraction < 1:
            raise BaselineError(f"metric regression threshold must be in [0, 1): {name}")

    def threshold(name: str) -> float:
        return float(overrides.get(name, fraction))

    failures: list[str] = []
    observations: list[str] = []
    for name in higher_is_better:
        expected = _positive_measurement(baseline, name)
        actual = _positive_measurement(candidate, name)
        minimum = expected * (1 - threshold(name))
        observations.append(f"{name}: {actual:.3f} (minimum {minimum:.3f})")
        if actual < minimum:
            failures.append(observations[-1])
    for name in lower_is_better:
        expected = _positive_measurement(baseline, name)
        actual = _positive_measurement(candidate, name)
        maximum = expected * (1 + threshold(name))
        observations.append(f"{name}: {actual:.3f} (maximum {maximum:.3f})")
        if actual > maximum:
            failures.append(observations[-1])
    if failures:
        raise BaselineError("performance regression: " + "; ".join(failures))
    return observations


def main() -> int:
    """Load two JSON paths and report the gated comparison."""
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", required=True, type=pathlib.Path)
    parser.add_argument("--candidate", required=True, type=pathlib.Path)
    args = parser.parse_args()
    try:
        baseline = json.loads(args.baseline.read_text(encoding="utf-8"))
        candidate = json.loads(args.candidate.read_text(encoding="utf-8"))
        observations = compare(baseline, candidate)
    except (OSError, json.JSONDecodeError, BaselineError) as error:
        print(f"performance gate failed: {error}", file=sys.stderr)
        return 1
    for observation in observations:
        print(observation)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
