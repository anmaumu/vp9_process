"""Summarize selected Xe tracepoints without claiming image-copy attribution.

Input: perf script --ns output. Optional journal requires --clockid mono at
capture time. Main-thread phase coincidence is NOT a causal buffer identity.
"""
import argparse
from bisect import bisect_right
from collections import Counter
from decimal import Decimal
import json
from pathlib import Path
import re

LINE = re.compile(r"^\s*.+?\s+(\d+)\s+\[\d+\]\s+(\d+\.\d+):\s+xe:(xe_\w+):\s+(.*)$")
MOVE = re.compile(r"move_lacks_source:(yes|no), migrate object (0x[0-9a-f]+) \[size (\d+)\] from (\w+) to (\w+) device_id:(\S+)")
DEVICE = re.compile(r"\bdev=(\S+?),")
EVENTS = {"xe_bo_move", "xe_bo_cpu_fault", "xe_sched_job_exec"}


def load_journal(lines):
    records = [json.loads(line) for line in lines if line.strip()]
    if not records or len(records) > 10000:
        raise ValueError("Missing or oversized journal")
    for index, record in enumerate(records):
        if not isinstance(record, dict):
            raise ValueError("Journal entry must be an object")
        if (record.get("version") != 1 or record.get("clock") != "CLOCK_MONOTONIC"
                or record.get("sequence") != index or not isinstance(record.get("phase"), str)
                or not isinstance(record.get("monotonic_ns"), int)
                or not isinstance(record.get("pid"), int) or record["pid"] <= 0
                or not isinstance(record.get("tid"), int) or record["tid"] <= 0
                or record.get("pid") != records[0].get("pid")
                or record.get("tid") != records[0].get("tid")):
            raise ValueError("Invalid journal schema or process identity")
        if index and record["monotonic_ns"] < records[index - 1]["monotonic_ns"]:
            raise ValueError("Nonmonotonic journal")
    if (records[0]["phase"] != "run_start" or records[-1]["phase"] != "run_complete"
            or records[-1].get("validation") != "passed"
            or any(r["phase"] == "run_failed" for r in records)):
        raise ValueError("Workload did not complete successfully")
    return records


def analyze(lines, records=None, device="0000:83:00.0"):
    counts, groups, sizes, phase_counts = Counter(), Counter(), Counter(), Counter()
    phase_moves = Counter()
    tids, addresses = set(), set()
    times = [r["monotonic_ns"] for r in records] if records else []
    correlated = 0
    for number, line in enumerate(lines, 1):
        if number > 1000000:
            raise ValueError("Trace exceeds diagnostic limit")
        if not line.strip() or line.startswith("#"):
            continue
        match = LINE.fullmatch(line.rstrip())
        if not match:
            raise ValueError(f"Unrecognized/lost/truncated trace record at line {number}")
        tid, seconds, event, detail = match.groups()
        if event not in EVENTS:
            raise ValueError(f"Unexpected event: {event}")
        tid = int(tid)
        tids.add(tid)
        timestamp = int(Decimal(seconds) * 1000000000)
        phase = "unattributed"
        if records and tid == records[0]["tid"] and times[0] <= timestamp < times[-1]:
            phase = records[bisect_right(times, timestamp) - 1]["phase"]
            correlated += 1
        phase_counts[(phase, event)] += 1
        counts[event] += 1
        if event == "xe_bo_move":
            move = MOVE.fullmatch(detail)
            if not move:
                raise ValueError("Unknown BO move schema")
            lacks, address, size, source, target, observed_device = move.groups()
            kind = "source_absent_clear_path" if lacks == "yes" else "source_present_migration_path"
            groups[(kind, source, target, observed_device)] += 1
            phase_moves[(phase, kind, source, target)] += 1
            sizes[(kind, int(size))] += 1
            if lacks == "no":
                addresses.add(address)  # Addresses can be reused; NOT unique object identities.
        else:
            dev = DEVICE.search(detail)
            if not dev:
                raise ValueError("Missing device identity")
            observed_device = dev[1]
            if event == "xe_bo_cpu_fault" and not re.search(r"\bsize=\d+, flags=0x[0-9a-f]+,", detail):
                raise ValueError("Unknown CPU fault schema")
            if event == "xe_sched_job_exec" and not re.search(r"\berror=0$", detail):
                raise ValueError("GPU job error or unknown job schema")
        if observed_device != device:
            raise ValueError(f"Unexpected GPU: {observed_device}")
    if not counts or not counts["xe_sched_job_exec"]:
        raise ValueError("No GPU execution evidence")
    if records and not correlated:
        raise ValueError("No matching journal/trace time and thread; check capture clock/run")
    return {
        "version": 1, "status": "observed_partial", "device": device,
        "complete_copy_proof": False, "image_buffer_attribution": "unresolved",
        "counts": dict(sorted(counts.items())), "observed_tids": sorted(tids),
        "moves": [dict(kind=k[0], source=k[1], target=k[2], device=k[3], count=v)
                  for k, v in sorted(groups.items())],
        "move_object_sizes": [dict(kind=k[0], object_size=k[1], count=v) for k, v in sorted(sizes.items())],
        "migration_distinct_address_values": len(addresses),
        "phases": [dict(phase=k[0], event=k[1], count=v) for k, v in sorted(phase_counts.items())],
        "phase_moves": [dict(phase=k[0], kind=k[1], source=k[2], target=k[3], count=v)
                        for k, v in sorted(phase_moves.items())],
        "surface_exports": [r for r in (records or []) if "layout" in r],
        "limitations": ["BO sizes are not transferred-byte counts; migration is observed before completion",
                        "Kernel address reuse is possible; DMA-BUF inode is not a kernel BO pointer",
                        "CPU faults do not identify pixel downloads",
                        "Main-thread phase coincidence is not causality; other threads remain unattributed",
                        "Process-scoped collection misses independent kernel workers and other copy mechanisms",
                        "Text absence of lost records does not prove complete capture; retain perf diagnostics",
                        "Diagnostic surface exports may perturb allocation/migration behavior"],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("events", type=Path)
    parser.add_argument("--journal", type=Path)
    parser.add_argument("--device", default="0000:83:00.0")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.resolve() in {p.resolve() for p in (args.events, args.journal) if p}:
        parser.error("Output must not overwrite input evidence")
    args.output.write_text('{"status":"not_completed"}\n', encoding="utf-8")
    try:
        records = load_journal(args.journal.read_text().splitlines()) if args.journal else None
        with args.events.open(encoding="utf-8") as stream:
            report = analyze(stream, records, args.device)
        args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    except (ValueError, OSError, KeyError, TypeError) as error:
        args.output.write_text(json.dumps({"status": "failed", "error": str(error)}) + "\n", encoding="utf-8")
        parser.exit(1, f"Trace analysis failed: {error}\n")


if __name__ == "__main__":
    main()
