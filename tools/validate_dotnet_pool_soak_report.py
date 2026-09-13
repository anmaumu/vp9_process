"""Validate a completed .NET CPU pool soak report without trusting its producer."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
from typing import Any


MAX_REPORT_BYTES = 1024 * 1024


def _mapping(value: Any, name: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ValueError(f"{name} must be an object")
    return value


def _integer(value: Any, name: str, *, minimum: int | None = None) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise ValueError(f"{name} must be an integer")
    if minimum is not None and value < minimum:
        raise ValueError(f"{name} must be at least {minimum}")
    return value


def _number(value: Any, name: str, *, minimum: float | None = None) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{name} must be a number")
    result = float(value)
    if not math.isfinite(result):
        raise ValueError(f"{name} must be finite")
    if minimum is not None and result < minimum:
        raise ValueError(f"{name} must be at least {minimum}")
    return result


def _bounded_measurement(value: Any, name: str) -> None:
    measurement = _mapping(value, name)
    growth = _integer(measurement.get("growth"), f"{name}.growth")
    limit = _integer(measurement.get("limit"), f"{name}.limit", minimum=0)
    _integer(measurement.get("baseline"), f"{name}.baseline", minimum=0)
    _integer(measurement.get("peak"), f"{name}.peak", minimum=0)
    _integer(measurement.get("final"), f"{name}.final", minimum=0)
    if growth > limit:
        raise ValueError(f"{name}.growth exceeds its limit")


def validate(report: dict[str, Any], *, minimum_seconds: float = 0.0) -> None:
    """Raise ``ValueError`` unless *report* proves a completed bounded soak."""

    if _integer(report.get("schema_version"), "schema_version") != 1:
        raise ValueError("unsupported schema_version")
    if report.get("status") != "passed" or report.get("failure") is not None:
        raise ValueError("soak did not complete successfully")
    _number(report.get("duration_seconds"), "duration_seconds", minimum=minimum_seconds)
    _integer(report.get("frames"), "frames", minimum=1)
    if report.get("page_locked") is not True:
        raise ValueError("page_locked must be true")
    capacity = _integer(report.get("capacity"), "capacity", minimum=1)
    minimum_peak = _integer(
        report.get("minimum_peak_in_use"), "minimum_peak_in_use", minimum=1)
    if minimum_peak > capacity:
        raise ValueError("minimum_peak_in_use exceeds capacity")

    gc = _mapping(report.get("gc"), "gc")
    collections = gc.get("collections")
    if not isinstance(collections, list) or len(collections) != 3:
        raise ValueError("gc.collections must contain Gen0, Gen1, and Gen2 counts")
    for generation, count in enumerate(collections):
        _integer(count, f"gc.collections[{generation}]", minimum=1)
    _bounded_measurement(gc.get("managed_bytes"), "gc.managed_bytes")
    _bounded_measurement(gc.get("pinned_objects"), "gc.pinned_objects")
    pinned = _mapping(gc.get("pinned_objects"), "gc.pinned_objects")
    if _integer(pinned.get("growth"), "gc.pinned_objects.growth") != 0:
        raise ValueError("managed pinned-object count did not return to baseline")

    process = _mapping(report.get("process"), "process")
    _bounded_measurement(process.get("private_bytes"), "process.private_bytes")

    pool = _mapping(report.get("pool"), "pool")
    if _integer(pool.get("capacity"), "pool.capacity") != capacity:
        raise ValueError("pool capacity does not match the requested capacity")
    if _integer(pool.get("in_use"), "pool.in_use", minimum=0) != 0:
        raise ValueError("pool still has an active lease")
    peak = _integer(pool.get("peak_in_use"), "pool.peak_in_use", minimum=minimum_peak)
    if peak > capacity:
        raise ValueError("pool peak occupancy exceeds capacity")
    allocation = _integer(pool.get("allocation_bytes"), "pool.allocation_bytes", minimum=1)
    locked = _integer(pool.get("page_locked_bytes"), "pool.page_locked_bytes", minimum=1)
    if locked < allocation:
        raise ValueError("page_locked_bytes is smaller than allocation_bytes")
    _integer(pool.get("acquisitions"), "pool.acquisitions", minimum=1)
    for field in (
        "rejected_acquisitions",
        "wait_nanoseconds",
        "lease_time_nanoseconds",
        "peak_lease_time_nanoseconds",
    ):
        _integer(pool.get(field), f"pool.{field}", minimum=0)


def validate_file(path: Path, *, minimum_seconds: float = 0.0) -> None:
    """Load and validate one size-bounded JSON report."""

    if path.stat().st_size > MAX_REPORT_BYTES:
        raise ValueError("report exceeds the maximum accepted size")
    report = json.loads(path.read_text(encoding="utf-8"))
    validate(_mapping(report, "report"), minimum_seconds=minimum_seconds)


def main() -> int:
    """Run the command-line report gate."""

    parser = argparse.ArgumentParser()
    parser.add_argument("report", type=Path)
    parser.add_argument("--minimum-seconds", type=float, default=0.0)
    arguments = parser.parse_args()
    try:
        validate_file(arguments.report, minimum_seconds=arguments.minimum_seconds)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
