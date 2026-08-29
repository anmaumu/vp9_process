import hashlib
import json
import pathlib
import tempfile
import unittest
from unittest import mock

from tools import collect_licenses


class CollectLicensesTests(unittest.TestCase):
    def test_locked_copy_and_hash_mismatch(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            source = root / "buildtrees/example/src/revision.clean/LICENSE"
            source.parent.mkdir(parents=True)
            source.write_text("permission text\n", encoding="utf-8")
            lock = root / "lock.json"
            lock.write_text(
                json.dumps({
                    "schema_version": 1,
                    "files": [{
                        "output": "example-LICENSE.txt",
                        "glob": "buildtrees/example/src/*clean/LICENSE",
                        "sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
                    }],
                }),
                encoding="utf-8",
            )
            output = root / "output"
            with mock.patch.object(collect_licenses, "LOCK", lock):
                collect_licenses.collect_locked(root, output)
                self.assertEqual(
                    (output / "example-LICENSE.txt").read_bytes(), source.read_bytes()
                )
                source.write_text("changed\n", encoding="utf-8")
                with self.assertRaises(collect_licenses.CollectionError):
                    collect_licenses.collect_locked(root, output)

    def test_nvcodec_license_blocks_are_deduplicated(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            include = root / "include"
            include.mkdir()
            block = "/* Permission is hereby granted to use this header. */\n"
            (include / "one.h").write_text(block + "int one;\n", encoding="utf-8")
            (include / "two.h").write_text(block + "int two;\n", encoding="utf-8")
            output = root / "output"
            output.mkdir()
            collect_licenses.collect_nvcodec(include, output)
            notices = (output / "nv-codec-headers-LICENSES.txt").read_text(
                encoding="utf-8"
            )
            self.assertEqual(notices.count("Permission is hereby granted"), 1)


if __name__ == "__main__":
    unittest.main()
