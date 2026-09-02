"""Interactive Linux qualification capture: sudo perf, unprivileged workload.

No sysctl, tracefs permissions, sudoers or global profiling settings are changed.
Run as the normal desktop/SSH user, NOT with sudo. The script asks for sudo only
for the fixed perf commands. Output stays in a fresh private temporary directory.
"""
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    if sys.platform != "linux" or os.geteuid() == 0:
        raise SystemExit("Run with python3 as a normal Linux user, not sudo")
    import pwd
    build = ROOT / "build/intel"
    fixture = build / "intel_av1_source.webm"
    if not fixture.is_file():
        fixture = build / "soak_source.webm"
    for path in (fixture, build / "libmkvcodec.so", Path("/usr/bin/perf"), Path("/usr/sbin/runuser")):
        if not path.is_file():
            raise SystemExit(f"Missing prerequisite: {path}")
    user = pwd.getpwuid(os.getuid()).pw_name
    authentication = subprocess.run(["sudo", "-v"])
    if authentication.returncode:
        raise SystemExit("Interactive sudo authentication is required in this session; capture was not started")
    output = Path(tempfile.mkdtemp(prefix="mkvc-kernel-phases-", dir="/tmp"))
    print(f"Capture directory: {output}", flush=True)
    command = ["sudo", "/usr/bin/perf", "record", "--clockid", "mono", "-o", str(output / "perf.data"),
               "-e", "xe:xe_bo_move", "-e", "xe:xe_bo_cpu_fault", "-e", "xe:xe_sched_job_exec",
               "--", "/usr/sbin/runuser", "-u", user, "--", "env",
               "MKVC_TEST_INTEL_DRM_RENDER_NODE=129", "MKVC_TEST_GPU_PCI=0000:83:00.0",
               "MKVC_REQUIRE_INTEL_EXTERNAL_IMPORT=1", "MKVC_OPENCL_OUTPUT_CODEC=av1",
               "MKVC_OPENCL_TEST_FRAMES=32", "MKVC_OPENCL_SOAK_SECONDS=0", "LIBVA_MESSAGING_LEVEL=0",
               "MKVC_OPENCL_REUSE_PROGRAM=0",  # Preserve the original privileged comparison baseline.
               f"MKVC_GPU_TRACE_JOURNAL={output / 'phases.jsonl'}",
               f"MKVC_OPENCL_SOAK_REPORT={output / 'workload.json'}",
               "timeout", "120s", "/usr/bin/python3", str(ROOT / "tests/python_intel_opencl_roundtrip.py"),
               str(build / "libmkvcodec.so"), str(build), str(ROOT / "python"), str(fixture)]
    manifest = {"version": 1, "status": "not_completed", "kernel": platform.release(),
                "clock": "CLOCK_MONOTONIC", "scope": "process and inherited tasks, not system-wide",
                "command": command, "surface_export_instrumentation": True,
                "opencl_reuse_program": False}
    manifest_path = output / "capture.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    try:
        with (output / "workload.log").open("w") as stdout, (output / "perf-record.log").open("w") as stderr:
            result = subprocess.run(command, cwd=ROOT, stdin=subprocess.DEVNULL,
                                    stdout=stdout, stderr=stderr, timeout=180)
        manifest["record_exit_code"] = result.returncode
        with (output / "events.txt").open("w") as stdout, (output / "perf-script.log").open("w") as stderr:
            script = subprocess.run(["sudo", "/usr/bin/perf", "script", "--ns", "-i", str(output / "perf.data")],
                                    stdin=subprocess.DEVNULL, stdout=stdout, stderr=stderr, timeout=60)
        manifest["script_exit_code"] = script.returncode
        if result.returncode or script.returncode:
            raise RuntimeError("Capture/workload failed; inspect logs, do not accept partial output")
        subprocess.run([sys.executable, str(ROOT / "tools/analyze_intel_kernel_trace.py"), str(output / "events.txt"),
                        "--journal", str(output / "phases.jsonl"), "--output", str(output / "analysis.json")], check=True)
        manifest["status"] = "observed_partial"
    except BaseException:
        manifest["status"] = "failed"
        raise
    finally:
        manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
        print(f"Results: {output}", flush=True)


if __name__ == "__main__":
    main()
