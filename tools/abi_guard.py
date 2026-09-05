#!/usr/bin/env python3
"""Validate the stable C ABI against a reviewed version snapshot."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "include" / "mkvcodec" / "mkvc.h"
DEFAULT_BASELINE = ROOT / "abi" / "mkvc-abi-v1.json"


class AbiGuardError(RuntimeError):
    """Raised when the public C ABI no longer matches its baseline."""


def _normalize(value: str) -> str:
    """Collapse C declaration whitespace.

    Parameters
    ----------
    value:
        C declaration fragment.

    Returns
    -------
    str
        A stable, single-line representation.
    """
    return " ".join(value.replace("\n", " ").split())


def _without_comments(source: str) -> str:
    """Remove C and C++ comments from a header string."""
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.DOTALL)
    return re.sub(r"//.*?$", "", source, flags=re.MULTILINE)


def _surface(header: Path = HEADER) -> dict[str, object]:
    """Extract the complete compatibility-sensitive surface of ``mkvc.h``.

    Parameters
    ----------
    header:
        Public C ABI header to inspect.

    Returns
    -------
    dict
        ABI version, exported signatures, enum values and struct fields.

    Raises
    ------
    AbiGuardError
        If the ABI version declaration cannot be found.
    """
    raw = header.read_text(encoding="utf-8")
    source = _without_comments(raw)
    version = re.search(r"#define\s+MKVC_ABI_VERSION\s+(\d+)u?", source)
    if version is None:
        raise AbiGuardError("MKVC_ABI_VERSION is missing")

    functions: dict[str, str] = {}
    for return_type, name, arguments in re.findall(
        r"^MKVC_API[ \t]+(.+?)[ \t]+(mkvc_\w+)\s*\((.*?)\);", source,
        flags=re.DOTALL | re.MULTILINE,
    ):
        functions[name] = _normalize(f"{return_type} {name}({arguments})")

    enums: dict[str, dict[str, int]] = {}
    for enum_name, body, alias in re.findall(
        r"typedef\s+enum\s+(\w+)\s*\{(.*?)\}\s*(\w+)\s*;", source,
        flags=re.DOTALL,
    ):
        if enum_name != alias:
            raise AbiGuardError(f"enum tag/alias mismatch: {enum_name}/{alias}")
        enums[alias] = {
            name: int(value)
            for name, value in re.findall(r"\b(MKVC_[A-Z0-9_]+)\s*=\s*(\d+)", body)
        }

    structs: dict[str, list[str]] = {}
    for tag, body, alias in re.findall(
        r"typedef\s+struct\s+(\w+)\s*\{(.*?)\}\s*(\w+)\s*;", source,
        flags=re.DOTALL,
    ):
        if tag != alias:
            raise AbiGuardError(f"struct tag/alias mismatch: {tag}/{alias}")
        structs[alias] = [
            _normalize(field) for field in body.split(";") if field.strip()
        ]

    if not functions or not enums or not structs:
        raise AbiGuardError("public ABI extraction returned an empty section")
    return {
        "abi_version": int(version.group(1)),
        "functions": dict(sorted(functions.items())),
        "enums": dict(sorted(enums.items())),
        "structs": dict(sorted(structs.items())),
    }


def snapshot(header: Path = HEADER) -> dict[str, object]:
    """Create a compact, reviewable fingerprint of the public ABI.

    Parameters
    ----------
    header:
        Public C ABI header to inspect.

    Returns
    -------
    dict
        ABI version, declaration counts, exported names and a SHA-256 digest
        over all normalized function signatures, enum values and struct fields.
    """
    surface = _surface(header)
    encoded = json.dumps(
        surface, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")
    functions = surface["functions"]
    enums = surface["enums"]
    structs = surface["structs"]
    return {
        "abi_version": surface["abi_version"],
        "surface_sha256": hashlib.sha256(encoded).hexdigest(),
        "function_count": len(functions),
        "enum_count": len(enums),
        "struct_count": len(structs),
        "symbols": sorted(functions),
    }


def validate(baseline: Path = DEFAULT_BASELINE, header: Path = HEADER) -> None:
    """Compare the current public header with a reviewed ABI snapshot.

    Parameters
    ----------
    baseline:
        Reviewed JSON snapshot committed for the ABI version.
    header:
        Public header to compare.

    Raises
    ------
    AbiGuardError
        If any compatibility-sensitive declaration differs.
    """
    expected = json.loads(baseline.read_text(encoding="utf-8"))
    actual = snapshot(header)
    if actual == expected:
        return
    differences = []
    for section in (
        "abi_version", "surface_sha256", "function_count", "enum_count",
        "struct_count", "symbols",
    ):
        if actual.get(section) != expected.get(section):
            differences.append(section)
    raise AbiGuardError(
        "public C ABI differs from the reviewed baseline in: "
        + ", ".join(differences)
        + "; review compatibility and update the snapshot deliberately"
    )


def main() -> int:
    """Run snapshot emission or fail-closed ABI validation."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("check", "snapshot"))
    parser.add_argument("--header", type=Path, default=HEADER)
    parser.add_argument("--baseline", type=Path, default=DEFAULT_BASELINE)
    args = parser.parse_args()
    try:
        value = snapshot(args.header)
        if args.command == "snapshot":
            print(json.dumps(value, indent=2, sort_keys=True))
        else:
            validate(args.baseline, args.header)
    except (AbiGuardError, OSError, json.JSONDecodeError) as error:
        print(f"abi-guard: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
