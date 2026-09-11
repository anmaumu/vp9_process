import pathlib
import struct
import tempfile
import unittest

from tools import pe_dependencies


def write_pe(
    path: pathlib.Path, imported: str | None = None, delay_imported: str | None = None
) -> None:
    data = bytearray(0x800)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    coff = 0x84
    struct.pack_into("<H", data, coff + 2, 1)
    struct.pack_into("<H", data, coff + 16, 0xF0)
    optional = coff + 20
    struct.pack_into("<H", data, optional, 0x20B)
    struct.pack_into("<Q", data, optional + 24, 0x140000000)
    struct.pack_into("<I", data, optional + 108, 16)
    if imported is not None:
        struct.pack_into("<II", data, optional + 112 + 8, 0x1000, 40)
        struct.pack_into("<IIIII", data, 0x400, 0, 0, 0, 0x1200, 0)
        data[0x600:0x600 + len(imported) + 1] = imported.encode() + b"\0"
    if delay_imported is not None:
        struct.pack_into("<II", data, optional + 112 + 13 * 8, 0x1100, 64)
        struct.pack_into("<IIIIIIII", data, 0x500, 1, 0x1240, 0, 0, 0, 0, 0, 0)
        data[0x640:0x640 + len(delay_imported) + 1] = delay_imported.encode() + b"\0"
    section = optional + 0xF0
    data[section:section + 8] = b".rdata\0\0"
    struct.pack_into("<IIII", data, section + 8, 0x1000, 0x1000, 0x400, 0x400)
    path.write_bytes(data)


class PeDependencyTests(unittest.TestCase):
    def test_reads_normal_and_delay_imports(self):
        with tempfile.TemporaryDirectory() as temporary:
            image = pathlib.Path(temporary) / "root.dll"
            write_pe(image, "codec.dll", "KERNEL32.dll")
            self.assertEqual(
                pe_dependencies.pe_imports(image),
                ("codec.dll", "KERNEL32.dll"),
            )

    def test_verifies_recursive_non_system_closure(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            core = root / "core.dll"
            codec = root / "codec.dll"
            helper = root / "helper.dll"
            write_pe(core, "codec.dll", "KERNEL32.dll")
            write_pe(codec, "helper.dll")
            write_pe(helper)
            self.assertEqual(
                pe_dependencies.verify_pe_dependency_closure(
                    (core,), (codec, helper)
                ),
                ("codec.dll", "core.dll", "helper.dll"),
            )
            with self.assertRaisesRegex(
                pe_dependencies.PeDependencyError, "unresolved.*helper.dll"
            ):
                pe_dependencies.verify_pe_dependency_closure((core,), (codec,))

    def test_rejects_invalid_or_unreachable_inputs(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            core = root / "core.dll"
            extra = root / "extra.dll"
            write_pe(core)
            write_pe(extra)
            with self.assertRaisesRegex(
                pe_dependencies.PeDependencyError, "unreachable"
            ):
                pe_dependencies.verify_pe_dependency_closure((core,), (extra,))
            core.write_bytes(b"not-pe")
            with self.assertRaisesRegex(pe_dependencies.PeDependencyError, "not a PE"):
                pe_dependencies.pe_imports(core)


if __name__ == "__main__":
    unittest.main()
