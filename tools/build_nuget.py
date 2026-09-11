#!/usr/bin/env python3
"""Build and inspect the RID-specific MkvCodec NuGet package."""

from __future__ import annotations

import argparse
import pathlib
import shutil
import subprocess
import tempfile
from collections.abc import Sequence

try:
    from .compliance_gate import (
        inspect_artifact, load_manifest, validate_project_license, write_sbom,
    )
except ImportError:
    from compliance_gate import (
        inspect_artifact, load_manifest, validate_project_license, write_sbom,
    )

ROOT = pathlib.Path(__file__).resolve().parents[1]


def build_nuget(
    dotnet: pathlib.Path,
    native: pathlib.Path,
    legal_dir: pathlib.Path,
    project_license: pathlib.Path,
    output_dir: pathlib.Path,
    rid: str,
    native_dependencies: Sequence[pathlib.Path] = (),
    qualification_only: bool = False,
) -> pathlib.Path:
    if rid not in {"win-x64", "linux-x64"}:
        raise ValueError("RID must be win-x64 or linux-x64")
    for path, label in (
        (dotnet, "dotnet executable"),
        (native, "native library"),
    ):
        if not path.is_file() or path.stat().st_size == 0:
            raise ValueError(f"{label} is missing or empty: {path}")
    validate_project_license(project_license, qualification_only=qualification_only)
    if not legal_dir.is_dir():
        raise ValueError(f"legal directory is missing: {legal_dir}")
    native_names = {native.name.casefold()}
    for dependency in native_dependencies:
        if not dependency.is_file() or dependency.stat().st_size == 0:
            raise ValueError(f"native dependency is missing or empty: {dependency}")
        if dependency.name.casefold() in native_names:
            raise ValueError(f"duplicate native library name: {dependency.name}")
        native_names.add(dependency.name.casefold())
    if not (legal_dir / "sbom.spdx.json").is_file():
        write_sbom(legal_dir / "sbom.spdx.json", load_manifest())
    output_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="mkvcodec-native-", dir=output_dir) as stage:
        native_stage = pathlib.Path(stage)
        for source in (native, *native_dependencies):
            shutil.copy2(source, native_stage / source.name)
        subprocess.run(
            [
                str(dotnet), "pack", str(ROOT / "dotnet/MkvCodec/MkvCodec.csproj"),
                "--configuration", "Release", "--output", str(output_dir),
                f"-p:RuntimeIdentifier={rid}",
                f"-p:MkvCodecNativeDirectory={native_stage.resolve()}",
                f"-p:MkvCodecLegalDir={legal_dir.resolve()}",
                f"-p:MkvCodecProjectLicense={project_license.resolve()}",
                f"-p:MkvCodecQualificationOnly={'true' if qualification_only else 'false'}",
            ],
            check=True,
        )
    packages = sorted(output_dir.glob("MkvCodec.0.1.0.nupkg"))
    if len(packages) != 1:
        raise RuntimeError(f"expected one NuGet package, found {len(packages)}")
    inspect_artifact(
        packages[0],
        load_manifest(),
        allow_qualification=qualification_only,
        required_native_names=(
            native.name,
            *(item.name for item in native_dependencies),
        ),
    )
    return packages[0]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dotnet", required=True, type=pathlib.Path)
    parser.add_argument("--native", required=True, type=pathlib.Path)
    parser.add_argument("--legal-dir", required=True, type=pathlib.Path)
    parser.add_argument("--project-license", required=True, type=pathlib.Path)
    parser.add_argument("--output-dir", required=True, type=pathlib.Path)
    parser.add_argument("--rid", required=True)
    parser.add_argument(
        "--native-dependency", action="append", default=[], type=pathlib.Path
    )
    parser.add_argument("--qualification-only", action="store_true")
    args = parser.parse_args()
    package = build_nuget(
        args.dotnet, args.native, args.legal_dir, args.project_license,
        args.output_dir, args.rid, args.native_dependency, args.qualification_only,
    )
    print(package)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
