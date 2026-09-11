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


CASE_KEYS = (
    "backend", "codec", "width", "height", "frames", "fps_nominal",
    "quality", "queue_size", "prefetch", "input_pixel_format",
    "output_pixel_format",
)
HIGHER_IS_BETTER = ("encode_fps", "decode_fps")
LOWER_IS_BETTER = (
    "write_latency_ms_p95", "decode_first_frame_ms",
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
    for key in CASE_KEYS:
        if baseline.get("case", {}).get(key) != candidate.get("case", {}).get(key):
            raise BaselineError(f"benchmark case mismatch: {key}")
    try:
        fraction = float(baseline["performance_gate"]["max_regression_fraction"])
    except (KeyError, TypeError, ValueError) as error:
        raise BaselineError("baseline has no numeric regression threshold") from error
    if not math.isfinite(fraction) or not 0 <= fraction < 1:
        raise BaselineError("max_regression_fraction must be in [0, 1)")

    failures: list[str] = []
    observations: list[str] = []
    for name in HIGHER_IS_BETTER:
        expected = _positive_measurement(baseline, name)
        actual = _positive_measurement(candidate, name)
        minimum = expected * (1 - fraction)
        observations.append(f"{name}: {actual:.3f} (minimum {minimum:.3f})")
        if actual < minimum:
            failures.append(observations[-1])
    for name in LOWER_IS_BETTER:
        expected = _positive_measurement(baseline, name)
        actual = _positive_measurement(candidate, name)
        maximum = expected * (1 + fraction)
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
