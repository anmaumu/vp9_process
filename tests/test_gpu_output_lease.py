from __future__ import annotations

import gc
import importlib.util
import sys
import unittest
from pathlib import Path

MODULE = Path(__file__).resolve().parents[1] / "python/mkvcodec/internal/gpu_output_lease.py"
SPEC = importlib.util.spec_from_file_location("mkvc_gpu_output_lease", MODULE)
assert SPEC is not None and SPEC.loader is not None
lease_api = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = lease_api
SPEC.loader.exec_module(lease_api)
RetainedDLPackProvider = lease_api.RetainedDLPackProvider
SharedGpuOutputLease = lease_api.SharedGpuOutputLease


class _Provider:
    def __dlpack_device__(self):
        return 14, 2

    def __dlpack__(self, **arguments):
        return "capsule", arguments


class _Retained:
    def __init__(self, capsule, owner):
        self.capsule = capsule
        self.owner = owner


class _Extension:
    @staticmethod
    def retain_owner(capsule, owner):
        return _Retained(capsule, owner)


class GpuOutputLeaseTests(unittest.TestCase):
    def test_dlpack_owner_delays_pool_reuse(self):
        events = []
        shared = SharedGpuOutputLease(
            wait=lambda timeout: events.append(("wait", timeout)),
            close_completion=lambda: events.append(("completion",)),
            release_slot=lambda: events.append(("slot",)),
        )
        image_token = shared.retain()
        provider = RetainedDLPackProvider(_Provider(), shared, _Extension())
        exported = provider.__dlpack__(stream=7)
        self.assertEqual(exported.capsule, ("capsule", {"stream": 7}))
        image_token.close()
        self.assertNotIn(("slot",), events)
        del exported
        gc.collect()
        self.assertEqual(events[-2:], [("completion",), ("slot",)])

    def test_last_python_token_waits_before_reuse(self):
        events = []
        shared = SharedGpuOutputLease(
            wait=lambda timeout: events.append(("wait", timeout)),
            close_completion=lambda: events.append(("completion",)),
            release_slot=lambda: events.append(("slot",)),
        )
        shared.retain().close()
        self.assertEqual(
            events,
            [("wait", 0xFFFFFFFF), ("completion",), ("slot",)],
        )


if __name__ == "__main__":
    unittest.main()
