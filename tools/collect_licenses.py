#!/usr/bin/env python3
"""Collect hash-locked third-party legal texts from verified build sources."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import shutil
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
LOCK = ROOT / "third_party" / "license-lock.json"
MANIFEST = ROOT / "third_party" / "manifest.json"


class CollectionError(RuntimeError):
    """License source is missing, ambiguous, or does not match its lock."""


def collect_locked(vcpkg_root: pathlib.Path, output: pathlib.Path) -> None:
    lock = json.loads(LOCK.read_text(encoding="utf-8"))
    if lock.get("schema_version") != 1:
        raise CollectionError("unsupported license lock schema")
    output.mkdir(parents=True, exist_ok=True)
    for record in lock["files"]:
        matches = list(vcpkg_root.glob(record["glob"]))
        if len(matches) != 1:
            raise CollectionError(
                f"{record['output']}: expected one source, found {len(matches)}"
            )
        content = matches[0].read_bytes()
        actual = hashlib.sha256(content).hexdigest()
        if actual != record["sha256"]:
            raise CollectionError(
                f"{record['output']}: SHA-256 {actual} does not match lock"
            )
        shutil.copyfile(matches[0], output / record["output"])


def collect_nvcodec(include: pathlib.Path, output: pathlib.Path) -> None:
    headers = sorted(include.glob("*.h"))
    if not headers:
        raise CollectionError("nv-codec-headers include directory contains no headers")
    blocks: list[str] = []
    for header in headers:
        text = header.read_text(encoding="utf-8", errors="strict")
        match = re.search(r"/\*.*?\*/", text, flags=re.DOTALL)
        if match is None or "Permission is hereby granted" not in match.group(0):
            raise CollectionError(f"{header.name}: MIT permission block not found")
        block = match.group(0).strip()
        if block not in blocks:
            blocks.append(block)
    destination = output / "nv-codec-headers-LICENSES.txt"
    heading = "nv-codec-headers n13.1.15.0 header license notices\n\n"
    destination.write_text(heading + "\n\n---\n\n".join(blocks) + "\n", encoding="utf-8")


def write_notices(output: pathlib.Path) -> None:
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    lines = [
        "# Third-Party Notices",
        "",
        "This distribution contains or was built using the components below.",
        "The referenced license and patent texts are part of this legal directory.",
        "",
        "| Component | Version | Distribution | License | Legal texts |",
        "|---|---|---|---|---|",
    ]
    for component in manifest["components"]:
        notices = ", ".join(f"[{name}]({name})" for name in component["required_notices"])
        lines.append(
            f"| {component['name']} | {component['version']} | "
            f"{component['distribution']} | {component['license']} | {notices or 'external dependency'} |"
        )
    lines.extend([
        "",
        "Vendor GPU drivers and runtimes are system dependencies and are not redistributed.",
        "Third-party names and marks are the property of their respective owners.",
        "This project is not endorsed by the listed projects or vendors.",
        "",
    ])
    (output / "THIRD_PARTY_NOTICES.md").write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--vcpkg-root", required=True, type=pathlib.Path)
    parser.add_argument("--nvcodec-include", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    args = parser.parse_args()
    try:
        collect_locked(args.vcpkg_root, args.output)
        collect_nvcodec(args.nvcodec_include, args.output)
        write_notices(args.output)
    except (CollectionError, OSError, json.JSONDecodeError) as error:
        print(f"license collection failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
