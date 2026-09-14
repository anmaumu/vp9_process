"""GPU-free regression tests for Intel USM fault evidence validation."""

from __future__ import annotations

import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from validate_usm_fault_report import validate  # noqa: E402


def valid_report() -> dict[str, object]:
    """Return the smallest complete Intel USM preview evidence record."""
    return {
        "validation": "passed",
        "producer_device": "Intel Arc",
        "other_device": "Intel Graphics",
        "same_context_export": "passed",
        "same_device_context": "equivalent_native_context",
        "cross_device": "rejected_by_oneapi_adapter",
        "dependency_failure": "propagated",
        "core_provenance": "opaque_caller_contract",
        "api_stability": "preview",
    }


class UsmFaultReportTest(unittest.TestCase):
    """Require every provenance, failure, and preview marker."""

    def test_accepts_complete_report(self) -> None:
        validate(valid_report())

    def test_accepts_distinct_same_device_context_rejection(self) -> None:
        report = valid_report()
        report["same_device_context"] = "distinct_context_rejected_by_oneapi_adapter"
        validate(report)

    def test_rejects_missing_or_false_evidence(self) -> None:
        for key in (
            "validation",
            "same_context_export",
            "same_device_context",
            "cross_device",
            "dependency_failure",
            "core_provenance",
            "api_stability",
        ):
            report = copy.deepcopy(valid_report())
            report[key] = "unknown"
            with self.assertRaises(ValueError, msg=key):
                validate(report)

    def test_rejects_duplicate_or_missing_devices(self) -> None:
        for value in ("Intel Arc", ""):
            report = valid_report()
            report["other_device"] = value
            with self.assertRaises(ValueError):
                validate(report)


if __name__ == "__main__":
    unittest.main()
