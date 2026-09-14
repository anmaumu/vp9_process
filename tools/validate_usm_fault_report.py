"""Fail-closed validator for Intel USM provenance/fault evidence."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def validate(report: dict[str, object]) -> None:
    """Raise ``ValueError`` unless all preview-boundary probes passed."""
    required = {
        "validation": "passed",
        "same_context_export": "passed",
        "cross_device": "rejected_by_oneapi_adapter",
        "dependency_failure": "propagated",
        "core_provenance": "opaque_caller_contract",
        "api_stability": "preview",
    }
    for name, expected in required.items():
        if report.get(name) != expected:
            raise ValueError(f"USM fault evidence is invalid: {name}")
    if report.get("same_device_context") not in {
        "equivalent_native_context",
        "distinct_context_rejected_by_oneapi_adapter",
    }:
        raise ValueError("USM same-device context evidence is invalid")
    producer = report.get("producer_device")
    other = report.get("other_device")
    if not isinstance(producer, str) or not producer or not isinstance(other, str) or not other:
        raise ValueError("USM device identity evidence is missing")
    if producer == other:
        raise ValueError("USM cross-device evidence used the producer device twice")


def main() -> None:
    """Validate one JSON evidence file from the command line."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    args = parser.parse_args()
    report = json.loads(args.report.read_text(encoding="utf-8"))
    validate(report)
    print(json.dumps({"validation": "passed", "report": str(args.report)}))


if __name__ == "__main__":
    main()
