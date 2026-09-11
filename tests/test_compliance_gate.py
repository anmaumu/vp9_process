import json
import pathlib
import tempfile
import unittest
import zipfile

from tools import compliance_gate


class ComplianceGateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.manifest = compliance_gate.load_manifest()

    def test_source_manifest_and_codec_exclusion(self):
        compliance_gate.validate_source_tree(self.manifest)

    def test_sbom_contains_all_components(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = pathlib.Path(temporary) / "sbom.spdx.json"
            compliance_gate.write_sbom(output, self.manifest)
            data = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(data["spdxVersion"], "SPDX-2.3")
            self.assertEqual(len(data["packages"]), len(self.manifest["components"]))

    def test_artifact_rejects_vendor_driver(self):
        with tempfile.TemporaryDirectory() as temporary:
            artifact = pathlib.Path(temporary) / "bad.whl"
            with zipfile.ZipFile(artifact, "w") as archive:
                archive.writestr("mkvcodec/nvEncodeAPI64.dll", b"driver")
            with self.assertRaises(compliance_gate.GateError):
                compliance_gate.inspect_artifact(artifact, self.manifest)

    def test_incomplete_artifact_fails_closed(self):
        with tempfile.TemporaryDirectory() as temporary:
            artifact = pathlib.Path(temporary) / "incomplete.nupkg"
            with zipfile.ZipFile(artifact, "w") as archive:
                archive.writestr("LICENSE", "project")
            with self.assertRaisesRegex(compliance_gate.GateError, "missing required"):
                compliance_gate.inspect_artifact(artifact, self.manifest)

    def test_complete_artifact_passes(self):
        with tempfile.TemporaryDirectory() as temporary:
            artifact = pathlib.Path(temporary) / "complete.whl"
            notices = {
                notice
                for component in self.manifest["components"]
                if component["distribution"] in {"bundled", "build-only"}
                for notice in component["required_notices"]
            }
            with zipfile.ZipFile(artifact, "w") as archive:
                archive.writestr("mkvcodec/mkvcodec.dll", "native")
                archive.writestr("mkvcodec-0.1.0.dist-info/RECORD", "record")
                prefix = "mkvcodec-0.1.0.dist-info/licenses/"
                archive.writestr(prefix + "LICENSE.txt", "project")
                archive.writestr(prefix + "THIRD_PARTY_NOTICES.md", "notices")
                archive.writestr(
                    prefix + "sbom.spdx.json",
                    json.dumps({"spdxVersion": "SPDX-2.3", "packages": []}),
                )
                for notice in notices:
                    archive.writestr(prefix + notice, "text")
            compliance_gate.inspect_artifact(artifact, self.manifest)
            with self.assertRaisesRegex(compliance_gate.GateError, "required native"):
                compliance_gate.inspect_artifact(
                    artifact,
                    self.manifest,
                    required_native_names=("missing-dependency.dll",),
                )

    def test_qualification_marker_requires_explicit_override(self):
        with tempfile.TemporaryDirectory() as temporary:
            artifact = pathlib.Path(temporary) / "qualification"
            artifact.mkdir()
            (artifact / "QUALIFICATION_ONLY.txt").write_text(
                "not for publication\n", encoding="utf-8"
            )
            with self.assertRaisesRegex(
                compliance_gate.GateError, "qualification-only"
            ):
                compliance_gate.inspect_artifact(artifact, self.manifest)

    def test_qualification_license_cannot_be_used_for_release(self):
        with tempfile.TemporaryDirectory() as temporary:
            license_path = pathlib.Path(temporary) / "LICENSE.txt"
            license_path.write_bytes(
                compliance_gate.QUALIFICATION_LICENSE_SENTINEL + b"\nfixture\n"
            )
            with self.assertRaisesRegex(
                compliance_gate.GateError, "qualification-only"
            ):
                compliance_gate.validate_project_license(license_path)
            compliance_gate.validate_project_license(
                license_path, qualification_only=True
            )


if __name__ == "__main__":
    unittest.main()
