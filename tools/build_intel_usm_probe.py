"""Build the Linux host-only SYCL qualification helper, never a shipped library."""
import argparse
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-root", type=Path, required=True)
    parser.add_argument("--opencl-headers", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if sys.platform != "linux":
        parser.error("This experimental helper requires Linux")
    root = args.runtime_root.resolve()
    headers = args.opencl_headers.resolve()
    for path in (root / "include/sycl/sycl.hpp", root / "lib/libsycl.so", headers / "CL/cl.h"):
        if not path.is_file():
            parser.error(f"Missing prerequisite: {path}")
    source = Path(__file__).resolve().parents[1] / "tests/probe_sycl_native.cpp"
    args.output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(["g++", "-std=c++17", "-DSYCL_DISABLE_FSYCL_SYCLHPP_WARNING",
                    "-Wno-deprecated-declarations", "-fPIC", "-shared", "-Wall", "-Wextra", "-Werror",
                    "-isystem", str(headers), "-isystem", str(root / "include"),
                    "-isystem", str(root / "include/sycl"), str(source),
                    "-L" + str(root / "lib"), "-lsycl", "-lze_loader", "-o", str(args.output)], check=True)


if __name__ == "__main__":
    main()
