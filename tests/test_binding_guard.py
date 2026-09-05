from __future__ import annotations

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
SPEC = importlib.util.spec_from_file_location(
    "binding_guard", ROOT / "tools" / "binding_guard.py")
assert SPEC and SPEC.loader
binding_guard = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(binding_guard)


class BindingGuardTests(unittest.TestCase):
    def test_checked_in_bindings_cover_public_abi(self) -> None:
        binding_guard.validate()

    def test_missing_python_declaration_fails(self) -> None:
        source = binding_guard.PYTHON_BINDING.read_text(encoding="utf-8")
        source = source.replace("lib.mkvc_get_version.argtypes", "lib.removed.argtypes", 1)
        source = source.replace("lib.mkvc_get_version.restype", "lib.removed.restype", 1)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "_native.py"
            path.write_text(source, encoding="utf-8")
            with self.assertRaises(binding_guard.BindingGuardError):
                binding_guard.validate(python_binding=path)

    def test_unknown_dotnet_declaration_fails(self) -> None:
        source = binding_guard.DOTNET_BINDING.read_text(encoding="utf-8")
        source += "\ninternal static extern void mkvc_unknown();\n"
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "NativeMethods.cs"
            path.write_text(source, encoding="utf-8")
            with self.assertRaises(binding_guard.BindingGuardError):
                binding_guard.validate(dotnet_binding=path)


if __name__ == "__main__":
    unittest.main()
