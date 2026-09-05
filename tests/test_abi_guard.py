from __future__ import annotations

import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "abi_guard", ROOT / "tools" / "abi_guard.py"
)
assert SPEC is not None and SPEC.loader is not None
abi_guard = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(abi_guard)


class AbiGuardTests(unittest.TestCase):
    """Protect the reviewed ABI snapshot and fail-closed behavior."""

    def test_current_header_matches_v1_baseline(self) -> None:
        """The checked-in public header must match the reviewed v1 surface."""
        abi_guard.validate()

    def test_signature_change_is_rejected(self) -> None:
        """A source-compatible-looking parameter edit must fail the gate."""
        source = abi_guard.HEADER.read_text(encoding="utf-8")
        source = source.replace(
            "mkvc_encoder_flush(mkvc_encoder* encoder)",
            "mkvc_encoder_flush(const mkvc_encoder* encoder)",
            1,
        )
        with tempfile.TemporaryDirectory() as temporary:
            header = Path(temporary) / "mkvc.h"
            header.write_text(source, encoding="utf-8")
            with self.assertRaises(abi_guard.AbiGuardError):
                abi_guard.validate(header=header)

    def test_incomplete_baseline_is_rejected(self) -> None:
        """A partial or accidentally regenerated baseline must not pass."""
        with tempfile.TemporaryDirectory() as temporary:
            baseline = Path(temporary) / "abi.json"
            baseline.write_text(json.dumps({"abi_version": 1}), encoding="utf-8")
            with self.assertRaises(abi_guard.AbiGuardError):
                abi_guard.validate(baseline=baseline)


if __name__ == "__main__":
    unittest.main()
