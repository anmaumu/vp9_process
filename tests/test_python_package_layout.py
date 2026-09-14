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

    def test_public_api_legacy_modules_are_thin_shims(self) -> None:
        moved = {
            "_capture.py": ("api/capture.py", "VideoCapture"),
            "_writer.py": ("api/writer.py", "VideoWriter"),
            "_capabilities.py": ("api/backend.py", "select_backend"),
            "_gpu.py": ("api/frame.py", "GpuFrame"),
            "_gpu_plane.py": ("interop/dlpack.py", "GpuPlane"),
        }
        for legacy_name, (canonical_name, public_name) in moved.items():
            legacy_tree = ast.parse((PACKAGE / legacy_name).read_text(encoding="utf-8"))
            canonical_tree = ast.parse((PACKAGE / canonical_name).read_text(encoding="utf-8"))
            definitions = (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)
            self.assertFalse(any(isinstance(node, definitions) for node in legacy_tree.body))
            self.assertTrue(
                any(
                    isinstance(node, definitions) and node.name == public_name
                    for node in canonical_tree.body
                )
            )

    def test_generated_native_implementation_lives_in_native_package(self) -> None:
        moved = {
            "_native.py": ("native/library.py", "check"),
            "_native_signatures.py": ("native/signatures.py", "configure"),
            "_native_types.py": ("native/types.py", "DecoderConfig"),
        }
        definitions = (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)
        for legacy_name, (canonical_name, definition_name) in moved.items():
            legacy_tree = ast.parse((PACKAGE / legacy_name).read_text(encoding="utf-8"))
            canonical_tree = ast.parse((PACKAGE / canonical_name).read_text(encoding="utf-8"))
            self.assertFalse(any(isinstance(node, definitions) for node in legacy_tree.body))
            self.assertTrue(
                any(
                    isinstance(node, definitions) and node.name == definition_name
                    for node in canonical_tree.body
                )
            )


if __name__ == "__main__":
    unittest.main()
