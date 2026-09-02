"""Conservative NEO evidence parsing, without a GPU or driver dependency."""
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from analyze_intel_userspace_trace import analyze


def allocation(handle, address):
    return (f"ThreadID: 7 Type: KERNEL_ISA Pool: LocalMemory Root index: 0 Size: 65536 "
            f"CPU VA: NULL GPU VA: 0xffff{address:012x} - 0xffffffffffffffff Handle: {handle}")


def binding(handle, address):
    return f"vm=1 obj=0x{handle:x} off=0x0 range=0x10000 addr=0x{address:x} operation=0(MAP) flags=2(IMMEDIATE) ret=0"


def fixture():
    records = [dict(phase="opencl_program_build", frame=0), dict(phase="opencl_enqueue_invert", frame=0)]
    lines = ["MKVC_PHASE " + json.dumps(records[0]), allocation(23, 0x800010000000),
             allocation(24, 0x800020000000), "MKVC_PHASE " + json.dumps(records[1]),
             binding(23, 0x800010000000), binding(24, 0x800020000000),
             "Created BO-21 range: e0000000 - e0006000, size: 24576 from PRIME_FD_TO_HANDLE"]
    return records, lines


class UserspaceTraceTests(unittest.TestCase):
    def test_matches_gpu_va_and_handle_not_kernel_identity(self):
        records, lines = fixture()
        report = analyze(lines, records, 1)
        self.assertFalse(report["complete_copy_proof"])
        self.assertEqual(report["isa_gpu_address_handle_matched_binds"][0]["count"], 2)

    def test_retired_or_reused_handle_does_not_match(self):
        for invalidation in ("GemClose h=0x17 r=0", "DRM_IOCTL_XE_GEM_CREATE has returned: 0 BO-23 with size: 65536"):
            records, lines = fixture()
            lines.insert(4, invalidation)
            report = analyze(lines, records, 1)
            self.assertEqual(report["isa_gpu_address_handle_matched_binds"][0]["count"], 1)

    def test_missing_or_malformed_evidence_rejected(self):
        records, lines = fixture()
        for changed in ([], lines[1:], [line.replace("ret=0", "ret=-5") for line in lines],
                        [line.replace("Size: 65536", "Size: bad") for line in lines]):
            with self.subTest(lines=changed), self.assertRaises(ValueError):
                analyze(changed, records, 1)

    def test_wrong_address_is_not_a_match(self):
        records, lines = fixture()
        lines[4] = binding(23, 0x999900000000)
        report = analyze(lines, records, 1)
        self.assertEqual(report["isa_gpu_address_handle_matched_binds"][0]["count"], 1)


if __name__ == "__main__":
    unittest.main()
