"""Fail-closed validator for Intel USM soak evidence."""
from __future__ import annotations

import argparse
import json
from pathlib import Path


def validate(report: dict, minimum_seconds: float = 0) -> None:
    """Raise ValueError unless a completed report satisfies all bounded gates."""
    if report.get("validation") != "passed":
        raise ValueError("USM soak did not complete successfully")
    requested = float(report.get("requested_seconds", -1))
    elapsed = float(report.get("elapsed_seconds", -1))
    batches = int(report.get("batches", 0))
    per_batch = int(report.get("frames_per_batch", 0))
    frames = int(report.get("frames", 0))
    if requested < minimum_seconds or elapsed < requested:
        raise ValueError("USM soak duration is shorter than required")
    if batches < (2 if requested > 0 else 1) or per_batch <= 0:
        raise ValueError("USM soak batch evidence is incomplete")
    if frames != batches * per_batch:
        raise ValueError("USM soak frame total is inconsistent")

    baseline = report.get("baseline", {})
    high_water = report.get("high_water", {})
    budgets = report.get("growth_budgets", {})
    for name in ("rss_bytes", "fds", "threads"):
        if name not in baseline or name not in high_water or name not in budgets:
            raise ValueError(f"USM soak resource evidence is missing: {name}")
        if high_water[name] > baseline[name] + budgets[name]:
            raise ValueError(f"USM soak resource budget exceeded: {name}")

    batch = report.get("last_batch", {})
    for name in ("owners_released", "allocations_released", "events_released",
                 "consumer_dependencies"):
        if batch.get(name) != per_batch:
            raise ValueError(f"USM soak ownership evidence is incomplete: {name}")
    if batch.get("validation") != "passed" or not batch.get("public_api"):
        raise ValueError("USM soak last batch did not use the public API successfully")

    gpu = report.get("gpu_memory", {})
    device = (gpu.get("processing_device") or {}).get("pci")
    active = (gpu.get("phases", {}).get("active", {}).get(device, {}))
    memory = active.get("high_water", {})
    if not any(key.startswith("drm-resident-vram") for key in memory):
        raise ValueError("USM soak has no active VRAM evidence for its processing GPU")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    parser.add_argument("--minimum-seconds", type=float, default=0)
    args = parser.parse_args()
    report = json.loads(args.report.read_text())
    validate(report, args.minimum_seconds)
    print(json.dumps({"validation": "passed", "report": str(args.report),
                      "elapsed_seconds": report["elapsed_seconds"],
                      "frames": report["frames"]}))


if __name__ == "__main__":
    main()
