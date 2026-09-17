#!/usr/bin/env python3
"""Build and audit an x86_64 wheel in the official manylinux_2_28 image."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_IMAGE = (
    "quay.io/pypa/manylinux_2_28_x86_64@"
    "sha256:531d7aa844bbb0c131d4ab011d3db741c4abc8d498cd5ccc86121046f62303b4"
)


def docker_command(
    docker: str,
    image: str,
    output: Path,
    cache: Path,
    *,
    qualification_only: bool,
) -> list[str]:
    """Create the deterministic container command for one wheel build."""
    command = [
        docker,
        "run",
        "--rm",
        "--volume",
        f"{ROOT}:/io:ro",
        "--volume",
        f"{output}:/out",
        "--volume",
        f"{cache}:/cache",
    ]
    if qualification_only:
        command.extend(("--env", "MKVC_QUALIFICATION_ONLY=1"))
    if os.name == "posix":
        command.extend(("--env", f"MKVC_HOST_UID={os.getuid()}"))
        command.extend(("--env", f"MKVC_HOST_GID={os.getgid()}"))
    command.extend((image, "bash", "/io/packaging/manylinux/build.sh"))
    return command


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--docker", default="docker")
    parser.add_argument("--image", default=DEFAULT_IMAGE)
    parser.add_argument("--output", type=Path, default=ROOT / "build/manylinux-wheel")
    parser.add_argument("--cache", type=Path, default=ROOT / "build/manylinux-cache")
    parser.add_argument("--qualification-only", action="store_true")
    arguments = parser.parse_args()

    docker = shutil.which(arguments.docker)
    if docker is None:
        parser.error(f"container command is unavailable: {arguments.docker}")
    output = arguments.output.resolve()
    cache = arguments.cache.resolve()
    if output.exists() and any(output.iterdir()):
        parser.error(f"output directory is not empty: {output}")
    output.mkdir(parents=True, exist_ok=True)
    cache.mkdir(parents=True, exist_ok=True)
    try:
        subprocess.run(
            docker_command(
                docker,
                arguments.image,
                output,
                cache,
                qualification_only=arguments.qualification_only,
            ),
            check=True,
        )
    except subprocess.CalledProcessError as error:
        return error.returncode
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
