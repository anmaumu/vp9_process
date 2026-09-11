import copy
import unittest

from tools import check_performance_baseline as baseline_gate


def record() -> dict:
    return {
        "schema_version": 1,
        "case": {
            "backend": "cpu", "codec": "vp9", "width": 1920,
            "height": 1080, "frames": 300, "fps_nominal": 60,
            "quality": 32, "queue_size": 8, "prefetch": 4,
            "input_pixel_format": "bgr24", "output_pixel_format": "bgr24",
        },
        "measurements": {
            "encode_fps": 100.0, "decode_fps": 120.0,
            "write_latency_ms_p95": 4.0, "decode_first_frame_ms": 20.0,
        },
        "performance_gate": {"max_regression_fraction": 0.10},
    }


class PerformanceBaselineTests(unittest.TestCase):
    def test_accepts_values_inside_directional_thresholds(self):
        baseline = record()
        candidate = copy.deepcopy(baseline)
        candidate["measurements"].update({
            "encode_fps": 90.0, "decode_fps": 108.0,
            "write_latency_ms_p95": 4.4, "decode_first_frame_ms": 22.0,
        })
        self.assertEqual(len(baseline_gate.compare(baseline, candidate)), 4)

    def test_rejects_regression_and_case_mismatch(self):
        baseline = record()
        candidate = copy.deepcopy(baseline)
        candidate["measurements"]["decode_fps"] = 100.0
        with self.assertRaisesRegex(baseline_gate.BaselineError, "decode_fps"):
            baseline_gate.compare(baseline, candidate)
        candidate = copy.deepcopy(baseline)
        candidate["case"]["width"] = 3840
        with self.assertRaisesRegex(baseline_gate.BaselineError, "width"):
            baseline_gate.compare(baseline, candidate)

    def test_rejects_missing_nonfinite_and_unapproved_threshold(self):
        baseline = record()
        candidate = copy.deepcopy(baseline)
        del candidate["measurements"]["encode_fps"]
        with self.assertRaises(baseline_gate.BaselineError):
            baseline_gate.compare(baseline, candidate)
        candidate = copy.deepcopy(baseline)
        candidate["measurements"]["encode_fps"] = float("nan")
        with self.assertRaises(baseline_gate.BaselineError):
            baseline_gate.compare(baseline, candidate)
        del baseline["performance_gate"]
        with self.assertRaisesRegex(baseline_gate.BaselineError, "threshold"):
            baseline_gate.compare(baseline, record())


if __name__ == "__main__":
    unittest.main()
