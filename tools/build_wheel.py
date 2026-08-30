#!/usr/bin/env python3
"""Build a platform wheel containing Python sources, native core, and legal payload."""

from __future__ import annotations

import argparse
import base64
import csv
import hashlib
import io
import pathlib
import re
import zipfile

try:
    from .compliance_gate import inspect_artifact, load_manifest, write_sbom
except ImportError:  # Direct script execution places tools/ on sys.path.
    from compliance_gate import inspect_artifact, load_manifest, write_sbom

ROOT = pathlib.Path(__file__).resolve().parents[1]
NAME = "mkvcodec"
VERSION = "0.1.0"


def digest(content: bytes) -> str:
    value = base64.urlsafe_b64encode(hashlib.sha256(content).digest()).rstrip(b"=")
    return "sha256=" + value.decode("ascii")


def build_wheel(
    native: pathlib.Path,
    legal_dir: pathlib.Path,
    project_license: pathlib.Path,
    output_dir: pathlib.Path,
    platform_tag: str,
    dlpack_extension: pathlib.Path | None = None,
) -> pathlib.Path:
    if not re.fullmatch(r"[A-Za-z0-9_.]+", platform_tag):
        raise ValueError("platform tag contains unsupported characters")
    for path, label in ((native, "native library"), (project_license, "project license")):
        if not path.is_file() or path.stat().st_size == 0:
            raise ValueError(f"{label} is missing or empty: {path}")
    if not legal_dir.is_dir():
        raise ValueError(f"legal directory is missing: {legal_dir}")
    if dlpack_extension is not None and (
        not dlpack_extension.is_file() or dlpack_extension.stat().st_size == 0
    ):
        raise ValueError(f"DLPack extension is missing or empty: {dlpack_extension}")

    output_dir.mkdir(parents=True, exist_ok=True)
    python_tag, abi_tag = ("cp39", "abi3") if dlpack_extension else ("py3", "none")
    wheel = output_dir / f"{NAME}-{VERSION}-{python_tag}-{abi_tag}-{platform_tag}.whl"
    dist_info = f"{NAME}-{VERSION}.dist-info"
    entries: dict[str, bytes] = {}
    for source in sorted((ROOT / "python" / NAME).glob("*.py")):
        entries[f"{NAME}/{source.name}"] = source.read_bytes()
    entries[f"{NAME}/{native.name}"] = native.read_bytes()
    if dlpack_extension is not None:
        entries[f"{NAME}/{dlpack_extension.name}"] = dlpack_extension.read_bytes()
    entries[f"{dist_info}/METADATA"] = (
        "Metadata-Version: 2.4\n"
        f"Name: {NAME}\nVersion: {VERSION}\n"
        "Summary: Python bindings for the mkvcodec VP9/AV1 C ABI\n"
        "Requires-Python: >=3.9\nRequires-Dist: numpy>=1.26\n"
        "License-File: licenses/LICENSE.txt\n\n"
    ).encode()
    entries[f"{dist_info}/WHEEL"] = (
        "Wheel-Version: 1.0\nGenerator: mkvcodec-build-wheel\n"
        "Root-Is-Purelib: false\n"
        f"Tag: {python_tag}-{abi_tag}-{platform_tag}\n\n"
    ).encode()
    license_prefix = f"{dist_info}/licenses"
    entries[f"{license_prefix}/LICENSE.txt"] = project_license.read_bytes()
    for source in sorted(legal_dir.iterdir()):
        if source.is_file():
            entries[f"{license_prefix}/{source.name}"] = source.read_bytes()
    sbom_path = legal_dir / "sbom.spdx.json"
    if not sbom_path.is_file():
        write_sbom(sbom_path, load_manifest())
        entries[f"{license_prefix}/{sbom_path.name}"] = sbom_path.read_bytes()

    record_name = f"{dist_info}/RECORD"
    record_buffer = io.StringIO(newline="")
    writer = csv.writer(record_buffer, lineterminator="\n")
    for name, content in sorted(entries.items()):
        writer.writerow((name, digest(content), len(content)))
    writer.writerow((record_name, "", ""))
    entries[record_name] = record_buffer.getvalue().encode()
    with zipfile.ZipFile(wheel, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for name, content in sorted(entries.items()):
            archive.writestr(name, content)
    inspect_artifact(wheel, load_manifest())
    return wheel


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--native", required=True, type=pathlib.Path)
    parser.add_argument("--legal-dir", required=True, type=pathlib.Path)
    parser.add_argument("--project-license", required=True, type=pathlib.Path)
    parser.add_argument("--output-dir", required=True, type=pathlib.Path)
    parser.add_argument("--platform-tag", required=True)
    parser.add_argument("--dlpack-extension", type=pathlib.Path)
    args = parser.parse_args()
    wheel = build_wheel(
        args.native, args.legal_dir, args.project_license, args.output_dir,
        args.platform_tag,
        args.dlpack_extension,
    )
    print(wheel)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
