from __future__ import annotations

import importlib.util
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("docgen", ROOT / "tools" / "docgen.py")
assert SPEC is not None and SPEC.loader is not None
docgen = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(docgen)


class DocgenTests(unittest.TestCase):
    def test_validation_counts_match_quality_gate(self) -> None:
        _, gate, ids = docgen.validate()
        self.assertEqual(gate["metrics"]["external_requirements"], len(ids["external"]))
        self.assertEqual(gate["metrics"]["internal_design_rules"], len(ids["internal"]))
        self.assertEqual(gate["metrics"]["test_requirements"], len(ids["tests"]))

    def test_generate_expected_outputs(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            docgen.generate(output)
            expected = (
                "index.md",
                "generated/traceability-matrix.md",
                "generated/quality-report.md",
                "api/c-abi.md",
                "api/python.md",
                "specification/external.md",
                "docgen.md",
            )
            for relative in expected:
                self.assertTrue((output / relative).is_file(), relative)
            c_api = (output / "api" / "c-abi.md").read_text(encoding="utf-8")
            self.assertIn("mkvc_encoder_create", c_api)
            python_api = (output / "api" / "python.md").read_text(encoding="utf-8")
            self.assertIn("VideoCapture", python_api)

    def test_all_c_abi_symbols_have_doxygen_comments(self) -> None:
        docgen.validate()
        header = (ROOT / "include" / "mkvcodec" / "mkvc.h").read_text(
            encoding="utf-8"
        )
        self.assertIn("/** Create a synchronous encoder.", header)
        self.assertIn("WARN_AS_ERROR          = FAIL_ON_WARNINGS",
                      (ROOT / "Doxyfile").read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
