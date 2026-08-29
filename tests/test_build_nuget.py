import pathlib
import tempfile
import unittest

from tools import build_nuget


class BuildNugetTests(unittest.TestCase):
    def test_rejects_missing_project_license_before_pack(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            dotnet = root / "dotnet"
            native = root / "libmkvcodec.so"
            dotnet.write_text("executable", encoding="utf-8")
            native.write_bytes(b"native")
            with self.assertRaisesRegex(ValueError, "project license"):
                build_nuget.build_nuget(
                    dotnet, native, root, root / "missing", root / "dist", "linux-x64"
                )

    def test_rejects_unknown_runtime_identifier(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            with self.assertRaisesRegex(ValueError, "RID"):
                build_nuget.build_nuget(
                    root, root, root, root, root / "dist", "osx-x64"
                )


if __name__ == "__main__":
    unittest.main()
