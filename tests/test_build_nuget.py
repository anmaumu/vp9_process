import pathlib
import tempfile
import unittest
import zipfile
from unittest import mock

from tools import build_nuget, compliance_gate


class BuildNugetTests(unittest.TestCase):
    def test_stages_all_native_libraries_in_rid_package(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            dotnet = root / "dotnet.exe"
            dotnet.write_bytes(b"test runner")
            native = root / "mkvcodec.dll"
            native.write_bytes(b"core")
            dependency = root / "dependency.dll"
            dependency.write_bytes(b"dependency")
            project_license = root / "LICENSE.txt"
            project_license.write_text("test license\n", encoding="utf-8")
            legal = root / "legal"
            legal.mkdir()
            manifest = compliance_gate.load_manifest()
            for component in manifest["components"]:
                if component["distribution"] == "dependency":
                    continue
                for notice in component["required_notices"]:
                    (legal / notice).write_text("legal text\n", encoding="utf-8")
            (legal / "THIRD_PARTY_NOTICES.md").write_text(
                "notices\n", encoding="utf-8"
            )
            compliance_gate.write_sbom(legal / "sbom.spdx.json", manifest)
            output = root / "dist"

            def fake_pack(command, *, check):
                self.assertTrue(check)
                stage_argument = next(
                    item for item in command
                    if item.startswith("-p:MkvCodecNativeDirectory=")
                )
                stage = pathlib.Path(stage_argument.split("=", 1)[1])
                self.assertEqual(
                    sorted(item.name for item in stage.iterdir()),
                    ["dependency.dll", "mkvcodec.dll"],
                )
                with zipfile.ZipFile(output / "MkvCodec.0.1.0.nupkg", "w") as archive:
                    archive.writestr("MkvCodec.nuspec", "package")
                    archive.writestr("lib/net8.0/MkvCodec.dll", "managed")
                    archive.writestr(
                        "runtimes/win-x64/native/mkvcodec.dll", "core"
                    )
                    archive.writestr(
                        "runtimes/win-x64/native/dependency.dll", "dependency"
                    )
                    archive.writestr("LICENSE.txt", "test license")
                    for source in legal.iterdir():
                        archive.write(source, f"licenses/{source.name}")

            with mock.patch.object(build_nuget.subprocess, "run", fake_pack):
                with mock.patch.object(
                    build_nuget, "verify_pe_dependency_closure"
                ) as verify_closure:
                    package = build_nuget.build_nuget(
                        dotnet, native, legal, project_license, output, "win-x64",
                        native_dependencies=(dependency,),
                    )
            verify_closure.assert_called_once_with((native,), (dependency,))
            self.assertEqual(package.name, "MkvCodec.0.1.0.nupkg")

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
