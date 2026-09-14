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
            "process_peak_rss_bytes": 100_000_000,
        },
        "performance_gate": {"max_regression_fraction": 0.10},
    }


def gpu_record() -> dict:
    return {
        "schema_version": 1,
        "case": {
            "backend": "intel", "input_codec": "vp9",
            "output_codec": "av1", "width": 1920,
            "height": 1080, "frames": 120, "fps_nominal": 30,
            "quality": 32, "require_gpu_resident": True,
            "input_sha256": "a" * 64,
        },
        "measurements": {
            "transcode_fps": 300.0, "submit_latency_ms_p95": 3.0,
            "process_peak_rss_bytes": 200_000_000,
        },
        "performance_gate": {"max_regression_fraction": 0.15},
    }


class PerformanceBaselineTests(unittest.TestCase):
    def test_accepts_values_inside_directional_thresholds(self):
        baseline = record()
        candidate = copy.deepcopy(baseline)
        candidate["measurements"].update({
            "encode_fps": 90.0, "decode_fps": 108.0,
            "write_latency_ms_p95": 4.4, "decode_first_frame_ms": 22.0,
            "process_peak_rss_bytes": 110_000_000,
        })
        self.assertEqual(len(baseline_gate.compare(baseline, candidate)), 5)

    def test_rejects_regression_and_case_mismatch(self):
        baseline = record()
        candidate = copy.deepcopy(baseline)
        candidate["measurements"]["decode_fps"] = 100.0
        with self.assertRaisesRegex(baseline_gate.BaselineError, "decode_fps"):
            baseline_gate.compare(baseline, candidate)
        candidate = copy.deepcopy(baseline)
        candidate["measurements"]["process_peak_rss_bytes"] = 120_000_000
        with self.assertRaisesRegex(baseline_gate.BaselineError, "process_peak_rss_bytes"):
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

    def test_supports_reviewed_per_metric_thresholds(self):
        baseline = record()
        baseline["performance_gate"]["metric_regression_fractions"] = {
            "decode_first_frame_ms": 0.30,
        }
        candidate = copy.deepcopy(baseline)
        candidate["measurements"]["decode_first_frame_ms"] = 25.0
        baseline_gate.compare(baseline, candidate)
        baseline["performance_gate"]["metric_regression_fractions"] = {
            "unknown": 0.1,
        }
        with self.assertRaisesRegex(baseline_gate.BaselineError, "unknown metric"):
            baseline_gate.compare(baseline, candidate)

    def test_gpu_transcode_profile(self):
        baseline = gpu_record()
        candidate = copy.deepcopy(baseline)
        candidate["measurements"].update({
            "transcode_fps": 255.0,
            "submit_latency_ms_p95": 3.44,
            "process_peak_rss_bytes": 229_000_000,
        })
        self.assertEqual(len(baseline_gate.compare(baseline, candidate)), 3)
        candidate["measurements"]["transcode_fps"] = 250.0
        with self.assertRaisesRegex(baseline_gate.BaselineError, "transcode_fps"):
            baseline_gate.compare(baseline, candidate)
        candidate = copy.deepcopy(baseline)
        candidate["case"].pop("require_gpu_resident")
        with self.assertRaisesRegex(baseline_gate.BaselineError, "profile"):
            baseline_gate.compare(baseline, candidate)


if __name__ == "__main__":
    unittest.main()
