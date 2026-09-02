"""Report gates must not turn missing instrumentation into a zero-copy pass."""
import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location("copy_audit", Path(__file__).with_name("run_gpu_copy_audit.py"))
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


class AuditGateTest(unittest.TestCase):
    def setUp(self):
        self.report = {
            "version": 1, "binding_conflicts": 0,
            "calls": {name: {"category": category, "bound": True,
                            "count": 1 if category in ("kernel", "sharing", "metadata") else 0}
                      for name, category in audit.EXPECTED.items()},
        }

    def test_expected_partial_observation(self):
        audit.validate(self.report)
        # A bitstream map is not automatically a raw-frame download.
        self.report["calls"]["vaMapBuffer"]["count"] = 9
        audit.validate(self.report)

    def test_each_host_api_rejected(self):
        for name, entry in self.report["calls"].items():
            if entry["category"] in ("host_transfer", "host_map"):
                report = copy.deepcopy(self.report)
                report["calls"][name]["count"] = 1
                with self.subTest(name=name), self.assertRaisesRegex(ValueError, "host transfer/map"):
                    audit.validate(report)

    def test_missing_or_conflicting_evidence_rejected(self):
        for mutation in (lambda r: r.update(binding_conflicts=1),
                         lambda r: r.update(version=2),
                         lambda r: r["calls"].pop("vaGetImage"),
                         lambda r: r["calls"]["clEnqueueNDRangeKernel"].update(count=0),
                         lambda r: r["calls"]["vaGetImage"].update(count=-1)):
            report = copy.deepcopy(self.report)
            mutation(report)
            with self.assertRaises(ValueError):
                audit.validate(report)

    def test_missing_report_invalidates_old_pass(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "result.json"
            output.write_text('{"validation":"passed"}')
            child = mock.MagicMock()
            child.__enter__.return_value = child
            child.wait.return_value = 0
            child.pid = 123
            args = ["audit", "--audit", "unused.so", "--report", str(output), "--", "child"]
            with mock.patch.object(sys, "argv", args), mock.patch.object(audit.subprocess, "Popen", return_value=child):
                with self.assertRaisesRegex(RuntimeError, "Missing audit report"):
                    audit.main()
            self.assertEqual(json.loads(output.read_text())["validation"], "not_completed")

    def test_wrong_process_report_rejected(self):
        child = mock.MagicMock()
        child.__enter__.return_value = child
        child.wait.return_value = 0
        child.pid = 123

        def launch(*args, **kwargs):
            Path(kwargs["env"]["MKVC_GPU_AUDIT_OUTPUT"]).write_text(json.dumps(dict(self.report, pid=456)))
            return child

        with mock.patch.object(sys, "argv", ["audit", "--audit", "unused.so", "--", "child"]), \
                mock.patch.object(audit.subprocess, "Popen", side_effect=launch):
            with self.assertRaisesRegex(ValueError, "process identity"):
                audit.main()

    def test_timeout_kills_and_reaps_child(self):
        child = mock.MagicMock()
        child.__enter__.return_value = child
        child.wait.side_effect = [subprocess.TimeoutExpired("child", 1), 0]
        with mock.patch.object(sys, "argv", ["audit", "--audit", "unused.so", "--", "child"]), \
                mock.patch.object(audit.subprocess, "Popen", return_value=child):
            with self.assertRaises(subprocess.TimeoutExpired):
                audit.main()
        child.kill.assert_called_once()
        self.assertEqual(child.wait.call_count, 2)


if __name__ == "__main__":
    unittest.main()
