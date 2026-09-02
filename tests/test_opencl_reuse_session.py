"""GPU-free lifetime/failure tests for the qualification-only OpenCL cache."""
import unittest
from intel_va_opencl_support import OpenClReuseSession


class ReuseTests(unittest.TestCase):
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
