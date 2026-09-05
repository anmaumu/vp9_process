from __future__ import annotations

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
SPEC = importlib.util.spec_from_file_location(
    "generate_bindings", ROOT / "tools" / "generate_bindings.py")
assert SPEC and SPEC.loader
generate_bindings = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(generate_bindings)


class GenerateBindingsTests(unittest.TestCase):
    def test_checked_in_python_signatures_are_reproducible(self) -> None:
        generate_bindings.check()

    def test_every_public_function_is_rendered(self) -> None:
        rendered = generate_bindings.render_python()
        functions = generate_bindings.abi_guard._surface()["functions"]
        for name in functions:
            self.assertIn(f"lib.{name}.argtypes", rendered)
            self.assertIn(f"lib.{name}.restype", rendered)

    def test_every_public_enum_and_struct_is_rendered(self) -> None:
        rendered = generate_bindings.render_python_types()
        surface = generate_bindings.abi_guard._surface()
        for values in surface["enums"].values():
            for name, value in values.items():
                self.assertIn(f"{name} = {value}", rendered)
        for c_name, fields in surface["structs"].items():
            class_name = generate_bindings._PYTHON_TYPES[c_name]
            self.assertIn(f"class {class_name}(ct.Structure):", rendered)
            for field in fields:
                _c_type, field_name, _array_size = generate_bindings._struct_field(
                    field
                )
                self.assertIn(f'(\"{field_name}\",', rendered)

    def test_dotnet_preserves_safe_handle_release_and_typed_status(self) -> None:
        rendered = generate_bindings.render_dotnet()
        self.assertIn("mkvc_gpu_frame_release(\n        nint frame);", rendered)
        self.assertIn(
            "mkvc_submission_query(\n        MkvSubmissionHandle submission,\n"
            "        out MkvSubmissionStatus out_status);",
            rendered,
        )

    def test_unmapped_public_type_fails_closed(self) -> None:
        source = generate_bindings.abi_guard.HEADER.read_text(encoding="utf-8")
        source = source.replace(
            "mkvc_result mkvc_get_version(mkvc_version* out_version)",
            "mkvc_result mkvc_get_version(unknown_type* out_version)")
        with tempfile.TemporaryDirectory() as directory:
            header = Path(directory) / "mkvc.h"
            header.write_text(source, encoding="utf-8")
            with self.assertRaises(generate_bindings.BindingGenerationError):
                generate_bindings.render_python(header)

    def test_unmapped_struct_field_type_fails_closed(self) -> None:
        source = generate_bindings.abi_guard.HEADER.read_text(encoding="utf-8")
        source = source.replace("uint32_t patch;", "unknown_field_type patch;", 1)
        with tempfile.TemporaryDirectory() as directory:
            header = Path(directory) / "mkvc.h"
            header.write_text(source, encoding="utf-8")
            with self.assertRaises(generate_bindings.BindingGenerationError):
                generate_bindings.render_python_types(header)


if __name__ == "__main__":
    unittest.main()
