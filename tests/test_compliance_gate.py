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


if __name__ == "__main__":
    unittest.main()
