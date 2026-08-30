import csv
import io
import pathlib
import tempfile
import unittest
import zipfile

from tools import build_wheel, compliance_gate


class BuildWheelTests(unittest.TestCase):
    def test_builds_inspectable_platform_wheel(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            native = root / "libmkvcodec.so"
            native.write_bytes(b"native-test")
            project_license = root / "PROJECT-LICENSE.txt"
            project_license.write_text("test project license\n", encoding="utf-8")
            legal = root / "legal"
            legal.mkdir()
            manifest = compliance_gate.load_manifest()
            for component in manifest["components"]:
                if component["distribution"] == "dependency":
                    continue
                for notice in component["required_notices"]:
                    (legal / notice).write_text("legal text\n", encoding="utf-8")
            (legal / "THIRD_PARTY_NOTICES.md").write_text("notices\n", encoding="utf-8")
            compliance_gate.write_sbom(legal / "sbom.spdx.json", manifest)
            wheel = build_wheel.build_wheel(
                native, legal, project_license, root / "dist", "manylinux_2_28_x86_64"
            )
            compliance_gate.inspect_artifact(wheel, manifest)
            with zipfile.ZipFile(wheel) as archive:
                names = archive.namelist()
                self.assertIn("mkvcodec/libmkvcodec.so", names)
                record_name = "mkvcodec-0.1.0.dist-info/RECORD"
                rows = list(csv.reader(io.StringIO(archive.read(record_name).decode())))
                self.assertEqual(len(rows), len(names))
                self.assertEqual(rows[-1], [record_name, "", ""])

            extension = root / "_dlpack.abi3.so"
            extension.write_bytes(b"abi3-extension")
            abi3_wheel = build_wheel.build_wheel(
                native, legal, project_license, root / "dist-abi3",
                "manylinux_2_28_x86_64", extension,
            )
            self.assertIn("cp39-abi3", abi3_wheel.name)
            with zipfile.ZipFile(abi3_wheel) as archive:
                self.assertIn("mkvcodec/_dlpack.abi3.so", archive.namelist())
                wheel_metadata = archive.read(
                    "mkvcodec-0.1.0.dist-info/WHEEL"
                ).decode()
                self.assertIn("Tag: cp39-abi3-manylinux_2_28_x86_64", wheel_metadata)

    def test_project_license_is_mandatory(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            native = root / "mkvcodec.dll"
            native.write_bytes(b"native-test")
            with self.assertRaisesRegex(ValueError, "project license"):
                build_wheel.build_wheel(
                    native, root, root / "missing", root / "dist", "win_amd64"
                )


if __name__ == "__main__":
    unittest.main()
