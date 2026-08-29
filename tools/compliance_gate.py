#!/usr/bin/env python3
"""Validate dependency metadata and inspect release artifacts.

The artifact command intentionally fails until a package contains its project
license, third-party notice index, SPDX SBOM, and every notice required by the
bundled dependency set. Vendor GPU runtimes are system dependencies and are
always forbidden package contents.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import sys
import zipfile
from collections.abc import Iterable

ROOT = pathlib.Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "third_party" / "manifest.json"
LICENSE_LOCK = ROOT / "third_party" / "license-lock.json"

FORBIDDEN_PACKAGE_NAMES = (
    re.compile(r"(^|/)(nvcuda|nvcuvid|nvencodeapi64)\.dll$", re.IGNORECASE),
    re.compile(r"(^|/)lib(cuda|nvcuvid|nvidia-encode)\.so(?:\..*)?$", re.IGNORECASE),
    re.compile(r"(^|/)(igdkmd64|igc64|igd12umd64)\.dll$", re.IGNORECASE),
    re.compile(r"(^|/)libigdgmm\.so(?:\..*)?$", re.IGNORECASE),
)
FORBIDDEN_CODEC_SYMBOLS = (
    "NV_ENC_CODEC_H264_GUID",
    "NV_ENC_CODEC_HEVC_GUID",
    "MFX_CODEC_AVC",
    "MFX_CODEC_HEVC",
    "V_MPEG4/ISO/AVC",
    "V_MPEGH/ISO/HEVC",
)
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hpp", ".cs", ".py"}


class GateError(RuntimeError):
    """Compliance validation failure."""


def load_manifest(path: pathlib.Path = MANIFEST) -> dict:
    data = json.loads(path.read_text(encoding="utf-8"))
    if data.get("schema_version") != 1 or not isinstance(data.get("components"), list):
        raise GateError("unsupported or malformed third-party manifest")
    required = {"name", "version", "license", "source", "distribution", "required_notices"}
    names: set[str] = set()
    for component in data["components"]:
        missing = required - component.keys()
        if missing:
            raise GateError(f"{component.get('name', '<unnamed>')}: missing {sorted(missing)}")
        name = component["name"]
        if name in names:
            raise GateError(f"duplicate component: {name}")
        names.add(name)
        if component["distribution"] not in {"bundled", "build-only", "dependency"}:
            raise GateError(f"{name}: invalid distribution")
        if not str(component["source"]).startswith("https://"):
            raise GateError(f"{name}: source must use HTTPS")
        if not isinstance(component["required_notices"], list):
            raise GateError(f"{name}: required_notices must be a list")
    return data


def validate_source_tree(manifest: dict) -> None:
    license_lock = json.loads(LICENSE_LOCK.read_text(encoding="utf-8"))
    locked_notices = {record["output"] for record in license_lock.get("files", [])}
    locked_notices.add("nv-codec-headers-LICENSES.txt")
    required_notices = {
        notice
        for component in manifest["components"]
        if component["distribution"] != "dependency"
        for notice in component["required_notices"]
    }
    if required_notices - locked_notices:
        raise GateError(
            f"required notices missing from license lock: {sorted(required_notices - locked_notices)}"
        )
    vcpkg = json.loads((ROOT / "vcpkg.json").read_text(encoding="utf-8"))
    covered = {component["name"].lower() for component in manifest["components"]}
    aliases = {"aom": "libaom", "svt-av1": "svt-av1"}
    missing = []
    for dependency in vcpkg["dependencies"]:
        name = dependency if isinstance(dependency, str) else dependency["name"]
        if aliases.get(name.lower(), name.lower()) not in covered and name != "pkgconf":
            missing.append(name)
    if missing:
        raise GateError(f"vcpkg dependencies missing from manifest: {missing}")

    violations = []
    for folder in (ROOT / "include", ROOT / "src", ROOT / "python", ROOT / "dotnet"):
        for path in folder.rglob("*"):
            if path.suffix.lower() not in SOURCE_SUFFIXES:
                continue
            text = path.read_text(encoding="utf-8", errors="ignore")
            for symbol in FORBIDDEN_CODEC_SYMBOLS:
                if symbol in text:
                    violations.append(f"{path.relative_to(ROOT)}: {symbol}")
    if violations:
        raise GateError("forbidden H.264/HEVC implementation symbols: " + ", ".join(violations))


def artifact_entries(path: pathlib.Path) -> dict[str, bytes]:
    if path.is_dir():
        return {
            item.relative_to(path).as_posix(): item.read_bytes()
            for item in path.rglob("*")
            if item.is_file()
        }
    if not zipfile.is_zipfile(path):
        raise GateError("artifact must be a directory, wheel, NuGet package, or ZIP")
    with zipfile.ZipFile(path) as archive:
        return {
            name.replace("\\", "/"): archive.read(name)
            for name in archive.namelist()
            if not name.endswith("/")
        }


def ends_with_any(names: Iterable[str], suffix: str) -> bool:
    normalized = suffix.lower().replace("\\", "/")
    return any(name.lower().replace("\\", "/").endswith(normalized) for name in names)


def inspect_artifact(path: pathlib.Path, manifest: dict) -> None:
    entries = artifact_entries(path)
    names = list(entries)
    forbidden = [name for name in names if any(pattern.search(name) for pattern in FORBIDDEN_PACKAGE_NAMES)]
    if forbidden:
        raise GateError(f"vendor GPU runtime/driver must not be bundled: {forbidden}")

    required = {"THIRD_PARTY_NOTICES.md", "sbom.spdx.json"}
    if not any(ends_with_any(names, candidate) for candidate in ("LICENSE", "LICENSE.txt")):
        required.add("project LICENSE or LICENSE.txt")
    for component in manifest["components"]:
        if component["distribution"] in {"bundled", "build-only"}:
            required.update(component["required_notices"])
    missing = sorted(item for item in required if item.startswith("project ") or not ends_with_any(names, item))
    if missing:
        raise GateError(f"artifact is missing required legal/SBOM files: {missing}")
    sbom_name = next(name for name in names if name.lower().endswith("sbom.spdx.json"))
    try:
        sbom = json.loads(entries[sbom_name].decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise GateError("artifact SBOM is not valid UTF-8 SPDX JSON") from error
    if sbom.get("spdxVersion") != "SPDX-2.3" or not isinstance(sbom.get("packages"), list):
        raise GateError("artifact SBOM must be an SPDX 2.3 document with packages")


def write_sbom(output: pathlib.Path, manifest: dict) -> None:
    packages = []
    for index, component in enumerate(manifest["components"], start=1):
        packages.append({
            "SPDXID": f"SPDXRef-Package-{index}",
            "name": component["name"],
            "versionInfo": component["version"],
            "downloadLocation": component["source"],
            "licenseConcluded": component["license"],
            "licenseDeclared": component["license"],
            "filesAnalyzed": False,
            "comment": f"Distribution classification: {component['distribution']}",
        })
    document = {
        "spdxVersion": "SPDX-2.3",
        "dataLicense": "CC0-1.0",
        "SPDXID": "SPDXRef-DOCUMENT",
        "name": "mkvcodec-dependency-sbom",
        "documentNamespace": "https://github.com/anmaumu/vp9_process/sbom/0.1.0",
        "creationInfo": {"creators": ["Tool: mkvcodec-compliance-gate"], "created": "2026-08-29T00:00:00Z"},
        "packages": packages,
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("source")
    artifact = subparsers.add_parser("artifact")
    artifact.add_argument("path", type=pathlib.Path)
    sbom = subparsers.add_parser("sbom")
    sbom.add_argument("output", type=pathlib.Path)
    args = parser.parse_args(argv)
    try:
        manifest = load_manifest()
        if args.command == "source":
            validate_source_tree(manifest)
        elif args.command == "artifact":
            inspect_artifact(args.path, manifest)
        else:
            write_sbom(args.output, manifest)
    except (GateError, OSError, json.JSONDecodeError) as error:
        print(f"compliance gate failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
