"""Unprivileged Arc diagnostic using NEO logging flags scoped to one child.

No perf, sudo, system settings or driver replacement. The installed NEO runtime
must support the requested logging; missing evidence fails closed. Linux only.
"""
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FLAGS = ("PrintBOCreateDestroyResult", "LogAllocationMemoryPool", "LogAllocationType",
         "LogAllocationStdout", "PrintXeLogs")


def main():
    if sys.platform != "linux" or os.geteuid() == 0:
        raise SystemExit("Run as an ordinary Linux user")
    build = ROOT / "build/intel"
    for path in (build / "libmkvcodec.so", build / "intel_av1_source.webm"):
        if not path.is_file():
            raise SystemExit(f"Missing prerequisite: {path}")
    output = Path(tempfile.mkdtemp(prefix="mkvc-userspace-", dir="/tmp"))
    runtime = subprocess.run(["dpkg-query", "-W", "-f=${Version}", "intel-opencl-icd"],
                             capture_output=True, text=True, check=True, timeout=10).stdout.strip()
    env = os.environ.copy()
    env.update({"NEOReadDebugKeys": "1", **{"NEO_" + key: "1" for key in FLAGS},
                "MKVC_TEST_INTEL_DRM_RENDER_NODE": "129", "MKVC_TEST_GPU_PCI": "0000:83:00.0",
                "MKVC_REQUIRE_INTEL_EXTERNAL_IMPORT": "1", "MKVC_OPENCL_OUTPUT_CODEC": "av1",
                "MKVC_OPENCL_TEST_FRAMES": "32", "MKVC_OPENCL_SOAK_SECONDS": "0",
                "MKVC_GPU_TRACE_JOURNAL": str(output / "phases.jsonl"), "MKVC_GPU_TRACE_STDOUT": "1",
                "MKVC_OPENCL_SOAK_REPORT": str(output / "workload.json"), "LIBVA_MESSAGING_LEVEL": "0"})
    command = ["timeout", "120s", "/usr/bin/python3", "-u", str(ROOT / "tests/python_intel_opencl_roundtrip.py"),
               str(build / "libmkvcodec.so"), str(build), str(ROOT / "python"), str(build / "intel_av1_source.webm")]
    manifest = {"version": 1, "status": "not_completed", "kernel": platform.release(),
                "command": command, "neo_runtime_package": runtime,
                "neo_logging_flags": list(FLAGS), "sudo": False}
    manifest_path = output / "capture.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    try:
        with (output / "driver.log").open("w") as log:
            result = subprocess.run(command, env=env, cwd=output, stdin=subprocess.DEVNULL,
                                    stdout=log, stderr=subprocess.STDOUT, timeout=150)
        manifest["exit_code"] = result.returncode
        result.check_returncode()
        subprocess.run([sys.executable, str(ROOT / "tools/analyze_intel_userspace_trace.py"),
                        str(output / "driver.log"), str(output / "phases.jsonl"), str(output / "workload.json"),
                        "--output", str(output / "analysis.json")], check=True)
        manifest["status"] = "observed_partial"
    except BaseException:
        manifest["status"] = "failed"
        raise
    finally:
        manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
        print(f"Results: {output}", flush=True)


if __name__ == "__main__":
    main()
