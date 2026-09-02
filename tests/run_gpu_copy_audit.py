"""Run a child under independent API instrumentation; fail closed on no report."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

EXPECTED = {
    **dict.fromkeys(("vaGetImage", "vaPutImage", "clEnqueueReadBuffer",
                     "clEnqueueWriteBuffer", "clEnqueueReadImage", "clEnqueueWriteImage"), "host_transfer"),
    **dict.fromkeys(("clEnqueueMapBuffer", "clEnqueueMapImage"), "host_map"),
    "vaDeriveImage": "metadata", "vaMapBuffer": "unclassified_map", "vaMapBuffer2": "unclassified_map",
    "clEnqueueNDRangeKernel": "kernel",
    "clEnqueueAcquireVA_APIMediaSurfacesINTEL": "sharing",
    "clEnqueueReleaseVA_APIMediaSurfacesINTEL": "sharing",
}


def validate(report, *, positive=True):
    if report.get("version") != 1 or report.get("binding_conflicts") != 0:
        raise ValueError("Invalid audit version or ambiguous function forwarding")
    calls = report["calls"]
    if set(calls) != set(EXPECTED):
        raise ValueError("Incomplete audit symbol table")
    for name, entry in calls.items():
        if (entry.get("category") != EXPECTED[name] or
                type(entry.get("count")) is not int or entry["count"] < 0 or
                type(entry.get("bound")) is not bool or
                (entry["count"] and not entry["bound"])):
            raise ValueError(f"Invalid audit entry: {name}")
    if positive:
        for entry in calls.values():
            if entry["category"] in ("host_transfer", "host_map") and entry["count"]:
                raise ValueError("Watched host transfer/map API was called")
        for name in ("clEnqueueNDRangeKernel", "clEnqueueAcquireVA_APIMediaSurfacesINTEL",
                     "clEnqueueReleaseVA_APIMediaSurfacesINTEL", "vaDeriveImage"):
            if not calls[name]["bound"] or calls[name]["count"] == 0:
                raise ValueError(f"Required observation missing: {name}")
    return report


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--audit", type=Path, required=True)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--timeout", type=float, default=90)
    parser.add_argument("--self-test", type=Path)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if args.report:
        args.report.write_text('{"validation":"not_completed"}\n')
    with tempfile.TemporaryDirectory(prefix="mkvc-copy-audit-") as directory:
        path = Path(directory) / "audit.json"
        env = dict(os.environ, LD_AUDIT=str(args.audit.resolve()), MKVC_GPU_AUDIT_OUTPUT=str(path))
        command = args.command
        if command[:1] == ["--"]:
            command = command[1:]
        if args.self_test:
            command = [sys.executable, str(Path(__file__).with_name("gpu_copy_audit_child.py")),
                       str(args.self_test.resolve())]
        if not command:
            parser.error("child command is required")
        with subprocess.Popen(command, env=env) as child:
            try:
                returncode = child.wait(timeout=args.timeout)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait()
                raise
            pid = child.pid
        if returncode == 77:
            return 77
        if returncode != 0:
            raise RuntimeError(f"Audited child failed: {returncode}")
        if not path.is_file():
            raise RuntimeError("Missing audit report; instrumentation may not have loaded")
        report = json.loads(path.read_text())
        report["validation"] = "not_completed"
        report["validation_mode"] = "injected_transfers" if args.self_test else "external_roundtrip"
        if args.report:
            args.report.write_text(json.dumps(report, indent=2) + "\n")
        if report.get("pid") != pid:
            raise ValueError("Invalid audit process identity")
        if args.self_test:
            validate(report, positive=False)
            calls = report["calls"]
            for name in EXPECTED:
                assert calls[name]["bound"] and calls[name]["count"] == (2 if name == "vaGetImage" else 1), name
            try:
                validate(report)
            except ValueError as error:
                assert "host transfer/map" in str(error)
            else:
                raise AssertionError("Injected host transfers were not rejected")
        else:
            validate(report)
        report["validation"] = "passed"
        if args.report:
            args.report.write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps(report, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
