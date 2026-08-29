#!/usr/bin/env python3
"""Build and inspect the RID-specific MkvCodec NuGet package."""

from __future__ import annotations

import argparse
import pathlib
import subprocess

try:
    from .compliance_gate import inspect_artifact, load_manifest, write_sbom
except ImportError:
    from compliance_gate import inspect_artifact, load_manifest, write_sbom

ROOT = pathlib.Path(__file__).resolve().parents[1]


def build_nuget(
    dotnet: pathlib.Path,
    native: pathlib.Path,
    legal_dir: pathlib.Path,
    project_license: pathlib.Path,
    output_dir: pathlib.Path,
    rid: str,
) -> pathlib.Path:
    if rid not in {"win-x64", "linux-x64"}:
        raise ValueError("RID must be win-x64 or linux-x64")
    for path, label in (
        (dotnet, "dotnet executable"),
        (native, "native library"),
        (project_license, "project license"),
    ):
        if not path.is_file() or path.stat().st_size == 0:
            raise ValueError(f"{label} is missing or empty: {path}")
    if not legal_dir.is_dir():
        raise ValueError(f"legal directory is missing: {legal_dir}")
    if not (legal_dir / "sbom.spdx.json").is_file():
        write_sbom(legal_dir / "sbom.spdx.json", load_manifest())
    output_dir.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [
            str(dotnet), "pack", str(ROOT / "dotnet/MkvCodec/MkvCodec.csproj"),
            "--configuration", "Release", "--output", str(output_dir),
            f"-p:RuntimeIdentifier={rid}",
            f"-p:MkvCodecNativePath={native.resolve()}",
            f"-p:MkvCodecLegalDir={legal_dir.resolve()}",
            f"-p:MkvCodecProjectLicense={project_license.resolve()}",
        ],
        check=True,
    )
    packages = sorted(output_dir.glob("MkvCodec.0.1.0.nupkg"))
    if len(packages) != 1:
        raise RuntimeError(f"expected one NuGet package, found {len(packages)}")
    inspect_artifact(packages[0], load_manifest())
    return packages[0]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dotnet", required=True, type=pathlib.Path)
    parser.add_argument("--native", required=True, type=pathlib.Path)
    parser.add_argument("--legal-dir", required=True, type=pathlib.Path)
    parser.add_argument("--project-license", required=True, type=pathlib.Path)
    parser.add_argument("--output-dir", required=True, type=pathlib.Path)
    parser.add_argument("--rid", required=True)
    args = parser.parse_args()
    package = build_nuget(
        args.dotnet, args.native, args.legal_dir, args.project_license,
        args.output_dir, args.rid,
    )
    print(package)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
