"""GPU-free DRM telemetry regression tests."""
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from gpu_resource_monitor import parse_fdinfo, snapshot, ResourceMonitor


class MonitorTest(unittest.TestCase):
    def test_units_and_missing(self):
        self.assertIsNone(parse_fdinfo("pos: 0"))
        header = "drm-driver: xe\ndrm-client-id: 1\ndrm-pdev: pci\n"
        parsed = parse_fdinfo(header + "drm-resident-vram0: 2 MiB\ndrm-total-system: 0")
        self.assertEqual(parsed["memory"]["drm-resident-vram0"], 2 * 1024**2)
        with self.assertRaises(ValueError):
            parse_fdinfo(header + "drm-total-system: 2 unknown")

    def test_duplicate_fds(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            value = "drm-driver: xe\ndrm-client-id: 1\ndrm-pdev: pci\ndrm-total-vram0: 4 KiB"
            (root / "1").write_text(value)
            (root / "2").write_text(value)
            self.assertEqual(snapshot(root)["pci"]["memory"]["drm-total-vram0"], 4096)

    def test_required_and_growth(self):
        monitor = ResourceMonitor(required=True)
        with patch("gpu_resource_monitor.snapshot", return_value={}):
            with self.assertRaisesRegex(RuntimeError, "unavailable"):
                monitor.sample("active")
        value = {"pci": {"driver": "xe", "memory": {"drm-resident-vram0": 4}}}
        monitor.set_processing_device({"pci": "pci"})
        with patch("gpu_resource_monitor.snapshot", return_value=value):
            monitor.sample("active")
            value["pci"]["memory"]["drm-resident-vram0"] += 300 * 1024**2
            with self.assertRaisesRegex(RuntimeError, "growth"):
                monitor.sample("active")


if __name__ == "__main__":
    unittest.main()
