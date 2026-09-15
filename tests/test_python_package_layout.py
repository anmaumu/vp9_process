"""Architecture guards for the fully layered Python package layout."""

from __future__ import annotations

import ast
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = ROOT / "python" / "mkvcodec"


class PythonPackageLayoutTests(unittest.TestCase):
    """Keep public, native, interop, and internal responsibilities separated."""

    def test_required_layer_implementations_exist(self) -> None:
        expected = {
            "api/backend.py": "select_backend",
            "api/capture.py": "VideoCapture",
            "api/frame.py": "GpuFrame",
            "api/metrics.py": "PipelineMetrics",
            "api/processor.py": "GpuProcessor",
            "api/video.py": "VideoInfo",
            "api/writer.py": "VideoWriter",
            "native/library.py": "check",
            "native/signatures.py": "configure",
            "native/types.py": "DecoderConfig",
            "interop/common.py": "import_external_frame",
            "interop/cupy_processor.py": "NvidiaCupyProcessorAdapter",
            "interop/dpnp_processor.py": "IntelDpnpProcessorAdapter",
            "interop/cuda.py": "_import_cuda_pointer",
            "interop/descriptor.py": "describe_interop",
            "interop/dlpack.py": "GpuPlane",
            "interop/frame_native.py": "get_gpu_frame_descriptor",
            "interop/intel.py": "_import_va_surface",
            "interop/validation.py": "frame_size",
            "internal/borrowed_cpu_frame.py": "BorrowedCpuFrame",
            "internal/cpu_array.py": "_BorrowedArray",
            "internal/cpu_pool.py": "CpuFramePool",
            "internal/encoder_config.py": "build_encoder_config",
            "internal/frame_outputs.py": "copy_packed",
            "internal/frame_views.py": "make_borrowed_view",
            "internal/gpu_pool.py": "_GpuResourceReservation",
            "internal/intel_usm_pool.py": "IntelUsmFramePool",
            "internal/intel_usm_slot.py": "IntelUsmPoolSlot",
            "internal/io_common.py": "_read_metrics",
            "internal/processing.py": "build_process_config",
            "internal/submission.py": "Submission",
            "internal/video_info.py": "probe_video",
        }
        definitions = (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)
        for relative, definition_name in expected.items():
            path = PACKAGE / relative
            self.assertTrue(path.is_file(), relative)
            tree = ast.parse(path.read_text(encoding="utf-8"))
            self.assertTrue(
                any(
                    isinstance(node, definitions) and node.name == definition_name
                    for node in tree.body
                ),
                f"{relative} does not define {definition_name}",
            )

    def test_root_initializer_only_aggregates_public_api(self) -> None:
        tree = ast.parse((PACKAGE / "__init__.py").read_text(encoding="utf-8"))
        imports = [node for node in tree.body if isinstance(node, ast.ImportFrom)]
        self.assertTrue(imports)
        self.assertTrue(all(node.level == 1 and node.module == "api" for node in imports))
        definitions = (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)
        self.assertFalse(any(isinstance(node, definitions) for node in tree.body))

    def test_no_flat_legacy_python_modules_remain(self) -> None:
        """Private compatibility shims were removed after the layered migration."""
        remaining = sorted(
            path.name for path in PACKAGE.glob("_*.py") if path.name != "__init__.py"
        )
        self.assertEqual(remaining, [])

    def test_layered_modules_do_not_import_removed_flat_modules(self) -> None:
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


if __name__ == "__main__":
    unittest.main()
