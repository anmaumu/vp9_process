"""Architecture guards for the layered Python package layout."""

from __future__ import annotations

import ast
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = ROOT / "python" / "mkvcodec"


class PythonPackageLayoutTests(unittest.TestCase):
    """Keep public, native, interop, and internal responsibilities separated."""

    def test_required_layers_exist(self) -> None:
        expected = {
            "api/capture.py",
            "api/writer.py",
            "api/frame.py",
            "api/backend.py",
            "api/metrics.py",
            "native/library.py",
            "native/signatures.py",
            "native/types.py",
            "interop/cuda.py",
            "interop/intel.py",
            "interop/dlpack.py",
            "internal/cpu_pool.py",
            "internal/submission.py",
        }
        missing = sorted(path for path in expected if not (PACKAGE / path).is_file())
        self.assertEqual(missing, [])

    def test_root_initializer_only_aggregates_public_api(self) -> None:
        tree = ast.parse((PACKAGE / "__init__.py").read_text(encoding="utf-8"))
        imports = [node for node in tree.body if isinstance(node, ast.ImportFrom)]
        self.assertTrue(imports)
        self.assertTrue(all(node.level == 1 and node.module == "api" for node in imports))
        forbidden = (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)
        self.assertFalse(any(isinstance(node, forbidden) for node in tree.body))

    def test_high_level_modules_use_native_package_boundary(self) -> None:
        legacy_import = "from . import _native as native"
        offenders = []
        for path in PACKAGE.glob("_*.py"):
            if path.name in {"_native.py", "_native_signatures.py", "_native_types.py"}:
                continue
            if legacy_import in path.read_text(encoding="utf-8"):
                offenders.append(path.name)
        self.assertEqual(offenders, [])


if __name__ == "__main__":
    unittest.main()
