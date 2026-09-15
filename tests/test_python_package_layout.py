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
            "interop/common.py",
            "interop/descriptor.py",
            "interop/frame_native.py",
            "interop/validation.py",
            "internal/cpu_pool.py",
            "internal/borrowed_cpu_frame.py",
            "internal/cpu_array.py",
            "internal/encoder_config.py",
            "internal/frame_outputs.py",
            "internal/frame_views.py",
            "internal/submission.py",
            "internal/io_common.py",
            "internal/processing.py",
            "internal/video_info.py",
            "internal/gpu_pool.py",
            "internal/intel_usm_pool.py",
            "internal/intel_usm_slot.py",
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
            "_cpu.py": ("internal/cpu_pool.py", "CpuFramePool"),
            "_submission.py": ("internal/submission.py", "Submission"),
            "_gpu_cuda.py": ("interop/cuda.py", "_import_cuda_pointer"),
            "_gpu_intel.py": ("interop/intel.py", "_import_va_surface"),
            "_gpu_import_common.py": ("interop/common.py", "import_external_frame"),
            "_gpu_import_validation.py": ("interop/validation.py", "frame_size"),
            "_gpu_frame_native.py": ("interop/frame_native.py", "get_gpu_frame_descriptor"),
            "_gpu_interop.py": ("interop/descriptor.py", "describe_interop"),
            "_gpu_resource_reservation.py": (
                "internal/gpu_pool.py",
                "_GpuResourceReservation",
            ),
            "_intel_usm.py": ("internal/intel_usm_pool.py", "IntelUsmFramePool"),
            "_intel_usm_slot.py": ("internal/intel_usm_slot.py", "IntelUsmPoolSlot"),
            "_types.py": ("api/frame.py", "CpuFrame"),
            "_borrowed_cpu_frame.py": (
                "internal/borrowed_cpu_frame.py",
                "BorrowedCpuFrame",
            ),
            "_cpu_array.py": ("internal/cpu_array.py", "_BorrowedArray"),
            "_encoder_config.py": (
                "internal/encoder_config.py",
                "build_encoder_config",
            ),
            "_frame_outputs.py": ("internal/frame_outputs.py", "copy_packed"),
            "_frame_views.py": ("internal/frame_views.py", "make_borrowed_view"),
            "_io_common.py": ("internal/io_common.py", "_read_metrics"),
            "_processing_plan.py": (
                "internal/processing.py",
                "build_process_config",
            ),
            "_video_info.py": ("internal/video_info.py", "probe_video"),
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

    def test_all_flat_private_python_modules_are_compatibility_only(self) -> None:
        """Prevent implementation from drifting back into the package root."""
        definitions = (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)
        offenders = []
        for path in PACKAGE.glob("_*.py"):
            tree = ast.parse(path.read_text(encoding="utf-8"))
            if any(isinstance(node, definitions) for node in tree.body):
                offenders.append(path.name)
        self.assertEqual(sorted(offenders), [])

    def test_layered_modules_do_not_depend_on_flat_compatibility_modules(self) -> None:
        """Keep compatibility imports one-way, from package root to a layer."""
        offenders = []
        for layer in ("api", "native", "interop", "internal"):
            for path in (PACKAGE / layer).glob("*.py"):
                tree = ast.parse(path.read_text(encoding="utf-8"))
                for node in ast.walk(tree):
                    if (
                        isinstance(node, ast.ImportFrom)
                        and node.level >= 2
                        and (node.module or "").startswith("_")
                    ):
                        offenders.append(f"{layer}/{path.name}:{node.module}")
        self.assertEqual(sorted(offenders), [])

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
