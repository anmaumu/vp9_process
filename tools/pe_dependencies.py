#!/usr/bin/env python3
"""Inspect the recursive Windows PE import dependency closure."""

from __future__ import annotations

import argparse
import pathlib
import re
import struct
import sys
from collections.abc import Sequence


class PeDependencyError(ValueError):
    """A PE image is malformed or its packaged dependency closure is invalid."""


_SYSTEM_DLLS = {
    "advapi32.dll", "bcrypt.dll", "cfgmgr32.dll", "comdlg32.dll",
    "crypt32.dll", "d3d11.dll", "d3d12.dll", "dbghelp.dll", "dxgi.dll",
    "gdi32.dll", "kernel32.dll", "msvcp140.dll", "msvcp140_1.dll",
    "msvcp140_2.dll", "ntdll.dll", "ole32.dll", "oleaut32.dll",
    "secur32.dll", "setupapi.dll", "shell32.dll", "shlwapi.dll",
    "ucrtbase.dll", "user32.dll", "userenv.dll", "version.dll",
    "vcruntime140.dll", "vcruntime140_1.dll", "winmm.dll", "ws2_32.dll",
}
_SYSTEM_PATTERNS = (
    re.compile(r"api-ms-win-.*\.dll", re.IGNORECASE),
    re.compile(r"ext-ms-win-.*\.dll", re.IGNORECASE),
    re.compile(r"python\d*\.dll", re.IGNORECASE),
)


def _u16(data: bytes, offset: int) -> int:
    if offset < 0 or offset + 2 > len(data):
        raise PeDependencyError("truncated PE integer")
    return struct.unpack_from("<H", data, offset)[0]


def _u32(data: bytes, offset: int) -> int:
    if offset < 0 or offset + 4 > len(data):
        raise PeDependencyError("truncated PE integer")
    return struct.unpack_from("<I", data, offset)[0]


def _u64(data: bytes, offset: int) -> int:
    if offset < 0 or offset + 8 > len(data):
        raise PeDependencyError("truncated PE integer")
    return struct.unpack_from("<Q", data, offset)[0]


def pe_imports(path: pathlib.Path) -> tuple[str, ...]:
    """Read normal and delay-load DLL names from a PE32/PE32+ image.

    Parameters
    ----------
    path:
        DLL or extension module to inspect.

    Returns
    -------
    tuple of str
        Case-preserving DLL names in deterministic order.

    Raises
    ------
    PeDependencyError
        If the file is not a supported, structurally valid PE image.
    """
    data = path.read_bytes()
    if len(data) < 64 or data[:2] != b"MZ":
        raise PeDependencyError(f"not a PE image: {path}")
    pe_offset = _u32(data, 0x3C)
    if pe_offset + 24 > len(data) or data[pe_offset:pe_offset + 4] != b"PE\0\0":
        raise PeDependencyError(f"invalid PE signature: {path}")
    coff = pe_offset + 4
    section_count = _u16(data, coff + 2)
    optional_size = _u16(data, coff + 16)
    optional = coff + 20
    if optional + optional_size > len(data):
        raise PeDependencyError(f"truncated PE optional header: {path}")
    magic = _u16(data, optional)
    if magic == 0x10B:
        directory_count_offset, directories_offset = 92, 96
        if optional_size < directories_offset:
            raise PeDependencyError(f"truncated PE32 optional header: {path}")
        image_base = _u32(data, optional + 28)
    elif magic == 0x20B:
        directory_count_offset, directories_offset = 108, 112
        if optional_size < directories_offset:
            raise PeDependencyError(f"truncated PE32+ optional header: {path}")
        image_base = _u64(data, optional + 24)
    else:
        raise PeDependencyError(f"unsupported PE optional-header magic: {path}")
    directory_count = _u32(data, optional + directory_count_offset)
    section_table = optional + optional_size
    if section_table + section_count * 40 > len(data):
        raise PeDependencyError(f"truncated PE section table: {path}")
    sections: list[tuple[int, int, int, int]] = []
    for index in range(section_count):
        section = section_table + index * 40
        sections.append((
            _u32(data, section + 12), _u32(data, section + 8),
            _u32(data, section + 20), _u32(data, section + 16),
        ))

    def rva_offset(rva: int) -> int:
        for virtual_address, virtual_size, raw_offset, raw_size in sections:
            span = max(virtual_size, raw_size)
            if virtual_address <= rva < virtual_address + span:
                delta = rva - virtual_address
                if delta >= raw_size or raw_offset + delta >= len(data):
                    break
                return raw_offset + delta
        raise PeDependencyError(f"unmapped PE RVA 0x{rva:x}: {path}")

    def c_string(rva: int) -> str:
        offset = rva_offset(rva)
        end = data.find(b"\0", offset, min(len(data), offset + 4096))
        if end < 0:
            raise PeDependencyError(f"unterminated PE import name: {path}")
        try:
            return data[offset:end].decode("ascii")
        except UnicodeDecodeError as error:
            raise PeDependencyError(f"non-ASCII PE import name: {path}") from error

    imports: dict[str, str] = {}

    def directory(index: int) -> tuple[int, int]:
        if directory_count <= index or directories_offset + (index + 1) * 8 > optional_size:
            return 0, 0
        entry = optional + directories_offset + index * 8
        return _u32(data, entry), _u32(data, entry + 4)

    import_rva, import_size = directory(1)
    if import_rva and import_size:
        descriptor = rva_offset(import_rva)
        limit = min(len(data), descriptor + import_size)
        while descriptor + 20 <= limit:
            values = struct.unpack_from("<IIIII", data, descriptor)
            if not any(values):
                break
            name = c_string(values[3])
            imports.setdefault(name.casefold(), name)
            descriptor += 20
        else:
            raise PeDependencyError(f"unterminated PE import directory: {path}")

    delay_rva, delay_size = directory(13)
    if delay_rva and delay_size:
        descriptor = rva_offset(delay_rva)
        limit = min(len(data), descriptor + delay_size)
        while descriptor + 32 <= limit:
            values = struct.unpack_from("<IIIIIIII", data, descriptor)
            if not any(values):
                break
            name_rva = values[1] if values[0] & 1 else values[1] - image_base
            name = c_string(name_rva)
            imports.setdefault(name.casefold(), name)
            descriptor += 32
        else:
            raise PeDependencyError(f"unterminated PE delay-import directory: {path}")
    return tuple(imports[key] for key in sorted(imports))


def is_system_dependency(name: str) -> bool:
    """Return whether a DLL is supplied by Windows or the host interpreter."""
    folded = name.casefold()
    return folded in _SYSTEM_DLLS or any(pattern.fullmatch(name) for pattern in _SYSTEM_PATTERNS)


def verify_pe_dependency_closure(
    roots: Sequence[pathlib.Path], dependencies: Sequence[pathlib.Path]
) -> tuple[str, ...]:
    """Verify that packaged DLLs exactly close all non-system PE imports.

    Parameters
    ----------
    roots:
        Independently loadable package entry points, such as the core DLL and
        Python extension.
    dependencies:
        Non-system DLLs intended to be bundled beside the roots.

    Returns
    -------
    tuple of str
        Reachable packaged filenames, including roots.

    Raises
    ------
    PeDependencyError
        If an import is unresolved, names collide, or a supplied dependency is
        unreachable from every root.
    """
    if not roots:
        raise PeDependencyError("at least one PE root is required")
    files: dict[str, pathlib.Path] = {}
    for path in (*roots, *dependencies):
        folded = path.name.casefold()
        if folded in files:
            raise PeDependencyError(f"duplicate packaged PE name: {path.name}")
        files[folded] = path
    queue = [path.name.casefold() for path in roots]
    reachable: set[str] = set()
    unresolved: dict[str, set[str]] = {}
    while queue:
        current = queue.pop(0)
        if current in reachable:
            continue
        reachable.add(current)
        for imported in pe_imports(files[current]):
            folded = imported.casefold()
            if folded in files:
                queue.append(folded)
            elif not is_system_dependency(imported):
                unresolved.setdefault(imported, set()).add(files[current].name)
    if unresolved:
        detail = ", ".join(
            f"{name} (imported by {', '.join(sorted(importers))})"
            for name, importers in sorted(unresolved.items())
        )
        raise PeDependencyError(f"unresolved non-system PE dependencies: {detail}")
    unused = sorted(path.name for path in dependencies if path.name.casefold() not in reachable)
    if unused:
        raise PeDependencyError(f"unreachable packaged PE dependencies: {unused}")
    return tuple(files[name].name for name in sorted(reachable))


def main() -> int:
    """Run dependency-closure verification from the command line."""
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", action="append", required=True, type=pathlib.Path)
    parser.add_argument("--dependency", action="append", default=[], type=pathlib.Path)
    args = parser.parse_args()
    try:
        closure = verify_pe_dependency_closure(args.root, args.dependency)
    except (OSError, PeDependencyError) as error:
        print(f"PE dependency check failed: {error}", file=sys.stderr)
        return 1
    for name in closure:
        print(name)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
