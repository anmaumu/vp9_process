"""GPU-free regression tests for fail-closed USM soak report acceptance."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from validate_usm_soak_report import validate


def valid_report():
    return {
        "validation": "passed", "requested_seconds": 1800,
        "elapsed_seconds": 1800.5, "batches": 3, "frames_per_batch": 32,
        "frames": 96, "baseline": {"rss_bytes": 10, "fds": 8, "threads": 12},
        "high_water": {"rss_bytes": 12, "fds": 8, "threads": 12},
        "growth_budgets": {"rss_bytes": 4, "fds": 2, "threads": 4},
        "last_batch": {"validation": "passed", "public_api": True,
                       "owners_released": 32, "allocations_released": 4,
                       "events_released": 32, "consumer_dependencies": 32,
                       "pool": {"capacity": 4, "peak_in_use": 4,
                                "backpressure": 1, "acquisitions": 32,
                                "rejected_acquisitions": 1, "wait_ns": 10}},
        "gpu_memory": {"processing_device": {"pci": "0000:83:00.0"},
                       "phases": {"active": {"0000:83:00.0": {
                           "high_water": {"drm-resident-vram0": 1}}}}},
    }


class UsmSoakReportTest(unittest.TestCase):
    def test_accepts_complete_report(self):
        validate(valid_report(), 1800)

    def test_rejects_incomplete_duration_and_counts(self):
        for key, value in (("validation", "not_completed"),
                           ("elapsed_seconds", 1799), ("frames", 95)):
            report = valid_report()
            report[key] = value
            with self.assertRaises(ValueError, msg=key):
                validate(report, 1800)

    def test_rejects_growth_owner_and_vram_gaps(self):
        reports = []
        report = valid_report()
        report["high_water"]["rss_bytes"] = 15
        reports.append(report)
        report = valid_report()
        report["last_batch"]["events_released"] = 31
        reports.append(report)
        report = valid_report()
        report["gpu_memory"]["phases"]["active"].clear()
        reports.append(report)
        for report in reports:
            with self.assertRaises(ValueError):
                validate(report, 1800)


if __name__ == "__main__":
    unittest.main()
