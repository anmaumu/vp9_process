"""Regression tests for fail-closed .NET CPU pool soak acceptance."""

from __future__ import annotations

import copy
import unittest

from tools.validate_dotnet_pool_soak_report import validate


def valid_report() -> dict[str, object]:
    """Return the smallest complete passing report."""

    bounded = {"baseline": 10, "peak": 20, "final": 12, "growth": 2, "limit": 4}
    return {
        "schema_version": 1,
        "status": "passed",
        "failure": None,
        "duration_seconds": 1800.1,
        "frames": 100,
        "page_locked": True,
        "capacity": 4,
        "minimum_peak_in_use": 2,
        "gc": {
            "collections": [1, 1, 1],
            "managed_bytes": copy.deepcopy(bounded),
            "pinned_objects": {
                "baseline": 1, "peak": 1, "final": 1, "growth": 0, "limit": 0
            },
        },
        "process": {"private_bytes": copy.deepcopy(bounded)},
        "pool": {
            "capacity": 4,
            "in_use": 0,
            "peak_in_use": 4,
            "allocation_bytes": 100,
            "page_locked_bytes": 4096,
            "acquisitions": 100,
            "rejected_acquisitions": 0,
            "wait_nanoseconds": 1,
            "lease_time_nanoseconds": 2,
            "peak_lease_time_nanoseconds": 1,
        },
    }


class DotNetPoolSoakReportTests(unittest.TestCase):
    def test_complete_report_passes(self) -> None:
        validate(valid_report(), minimum_seconds=1800)

    def test_failed_or_short_report_is_rejected(self) -> None:
        report = valid_report()
        report["status"] = "failed"
        with self.assertRaisesRegex(ValueError, "did not complete"):
            validate(report)
        report = valid_report()
        report["duration_seconds"] = 1799.9
        with self.assertRaisesRegex(ValueError, "at least"):
            validate(report, minimum_seconds=1800)

    def test_resource_and_pin_growth_are_rejected(self) -> None:
        report = valid_report()
        report["gc"]["managed_bytes"]["growth"] = 5  # type: ignore[index]
        with self.assertRaisesRegex(ValueError, "exceeds"):
            validate(report)
        report = valid_report()
        report["gc"]["pinned_objects"]["growth"] = 1  # type: ignore[index]
        with self.assertRaisesRegex(ValueError, "exceeds|baseline"):
            validate(report)

    def test_pool_leak_and_capacity_violation_are_rejected(self) -> None:
        report = valid_report()
        report["pool"]["in_use"] = 1  # type: ignore[index]
        with self.assertRaisesRegex(ValueError, "active lease"):
            validate(report)
        report = valid_report()
        report["pool"]["peak_in_use"] = 5  # type: ignore[index]
        with self.assertRaisesRegex(ValueError, "exceeds capacity"):
            validate(report)

    def test_missing_gc_generation_is_rejected(self) -> None:
        report = valid_report()
        report["gc"]["collections"] = [1, 1]  # type: ignore[index]
        with self.assertRaisesRegex(ValueError, "Gen0"):
            validate(report)


if __name__ == "__main__":
    unittest.main()
