from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
import unittest
from unittest import mock


MODULE = Path(__file__).resolve().parents[1] / "tools/build_manylinux_wheel.py"
SPEC = importlib.util.spec_from_file_location("mkvc_build_manylinux", MODULE)
assert SPEC is not None and SPEC.loader is not None
build_manylinux = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = build_manylinux
SPEC.loader.exec_module(build_manylinux)


class BuildManylinuxWheelTests(unittest.TestCase):
    def test_command_pins_image_and_separates_source_output_cache(self) -> None:
        with mock.patch.object(build_manylinux.os, "name", "nt"):
            command = build_manylinux.docker_command(
                "docker",
                build_manylinux.DEFAULT_IMAGE,
                Path("C:/output"),
                Path("C:/cache"),
                qualification_only=True,
            )
        self.assertTrue(any("@sha256:" in argument for argument in command))
        self.assertIn(f"{build_manylinux.ROOT}:/io:ro", command)
        self.assertIn(f"{Path('C:/output')}:/out", command)
        self.assertIn(f"{Path('C:/cache')}:/cache", command)
        self.assertIn("MKVC_QUALIFICATION_ONLY=1", command)
        self.assertEqual(command[-2:], ["bash", "/io/packaging/manylinux/build.sh"])

    def test_formal_command_omits_qualification_override(self) -> None:
        with mock.patch.object(build_manylinux.os, "name", "nt"):
            command = build_manylinux.docker_command(
                "docker",
                "example.invalid/manylinux@sha256:123",
                Path("C:/output"),
                Path("C:/cache"),
                qualification_only=False,
            )
        self.assertNotIn("MKVC_QUALIFICATION_ONLY=1", command)


if __name__ == "__main__":
    unittest.main()
