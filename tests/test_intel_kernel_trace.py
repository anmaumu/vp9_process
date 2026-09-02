"""GPU-free regression checks for conservative kernel trace interpretation."""
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from analyze_intel_kernel_trace import analyze, load_journal


def event(name, detail, stamp="1.500000000", tid=42):
    return f" python3 {tid} [015] {stamp}: xe:{name}: {detail}"


JOB = event("xe_sched_job_exec", "dev=0000:83:00.0, fence=0x123, error=0")
MOVE = "move_lacks_source:{}, migrate object 0x123 [size 65536] from gtt to vram0 device_id:0000:83:00.0"


def journal():
    return [dict(version=1, clock="CLOCK_MONOTONIC", pid=42, tid=42, sequence=i,
                 monotonic_ns=t, phase=p, validation="passed")
            for i, (t, p) in enumerate([(1000000000, "run_start"),
                                       (1200000000, "encoder_submit"),
                                       (2000000000, "run_complete")])]


class TraceTests(unittest.TestCase):
    def test_clear_distinct_from_copy_no_success_claim(self):
        report = analyze([JOB, event("xe_bo_move", MOVE.format("yes")),
                          event("xe_bo_move", MOVE.format("no"))])
        self.assertEqual(report["counts"]["xe_bo_move"], 2)
        self.assertEqual(len(report["moves"]), 2)
        self.assertFalse(report["complete_copy_proof"])
        self.assertEqual(report["image_buffer_attribution"], "unresolved")

    def test_phase_only_main_thread(self):
        records = load_journal(map(json.dumps, journal()))
        report = analyze([JOB, event("xe_sched_job_exec", "dev=0000:83:00.0, error=0", tid=43)], records)
        self.assertEqual({r["phase"] for r in report["phases"]}, {"encoder_submit", "unattributed"})

    def test_fail_closed(self):
        for lines in ([], ["LOST 10 events"], [JOB, "truncated"],
                      [JOB.replace("83:00.0", "00:02.0")],
                      [JOB.replace("error=0", "error=-5")],
                      [JOB, event("xe_bo_move", "unknown schema")]):
            with self.subTest(lines=lines), self.assertRaises(ValueError):
                analyze(lines)

    def test_mismatched_run_and_incomplete_journal(self):
        with self.assertRaises(ValueError):
            analyze([JOB.replace("1.500000000", "9.500000000")], journal())
        for records in ([], journal()[:-1], list(reversed(journal()))):
            with self.assertRaises(ValueError):
                load_journal(map(json.dumps, records))


if __name__ == "__main__":
    unittest.main()
