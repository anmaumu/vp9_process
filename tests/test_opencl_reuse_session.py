"""GPU-free lifetime/failure tests for the qualification-only OpenCL cache."""
import unittest
from unittest.mock import patch
from pathlib import Path
import os
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from capture_intel_userspace_trace import parse_args
from intel_va_opencl_support import OpenClReuseSession, reuse_program_enabled


class ReuseTests(unittest.TestCase):
    def test_reuse_default_and_explicit_baseline(self):
        with patch.dict(os.environ, {}, clear=True):
            self.assertTrue(reuse_program_enabled())
            for value, expected in (("0", False), ("1", True)):
                os.environ["MKVC_OPENCL_REUSE_PROGRAM"] = value
                self.assertEqual(reuse_program_enabled(), expected)

    def test_invalid_reuse_mode_rejected(self):
        for value in ("", "true", "2"):
            with patch.dict(os.environ, {"MKVC_OPENCL_REUSE_PROGRAM": value}):
                with self.assertRaises(ValueError):
                    reuse_program_enabled()

    def test_capture_modes_are_explicit_and_exclusive(self):
        self.assertTrue(parse_args([]).reuse_program)
        self.assertTrue(parse_args(["--reuse-program"]).reuse_program)
        self.assertFalse(parse_args(["--recreate-program"]).reuse_program)
        with patch("sys.stderr"), self.assertRaises(SystemExit) as failure:
            parse_args(["--reuse-program", "--recreate-program"])
        self.assertEqual(failure.exception.code, 2)

    def test_explicit_close_preserves_anchor_during_reverse_release(self):
        session = OpenClReuseSession()
        anchor = object()
        session.bind_device(anchor, 1, 2)
        released = []
        def release(name):
            self.assertIs(session.anchor, anchor)
            released.append(name)
        session.stack.callback(release, "context")
        session.stack.callback(release, "program")
        session.stack.callback(release, "kernel")
        session.resources = (1, 2, 3, 4)
        session.close()
        session.close()
        self.assertEqual(released, ["kernel", "program", "context"])
        self.assertIsNone(session.anchor)
        self.assertIsNone(session.resources)
        with self.assertRaises(RuntimeError), session.use():
            pass

    def test_failure_is_terminal_and_releases_partial_setup(self):
        session = OpenClReuseSession()
        released = []
        session.stack.callback(released.append, "context")
        with self.assertRaises(ValueError), session.use():
            raise ValueError("injected setup/dispatch failure")
        self.assertTrue(session.closed)
        self.assertEqual(session.completed_calls, 0)
        self.assertEqual(released, ["context"])

    def test_cross_device_display_rejected(self):
        for display, device in ((9, 2), (1, 9)):
            session = OpenClReuseSession()
            session.bind_device(object(), 1, 2)
            with self.assertRaises(RuntimeError), session.use():
                session.bind_device(object(), display, device)
            self.assertTrue(session.closed)

    def test_busy_close_and_concurrent_use_rejected(self):
        with OpenClReuseSession() as session:
            with session.use():
                with self.assertRaises(RuntimeError):
                    session.close()
                with self.assertRaises(RuntimeError), session.use():
                    pass
            self.assertEqual(session.completed_calls, 1)


if __name__ == "__main__":
    unittest.main()
