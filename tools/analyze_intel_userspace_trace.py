"""Interpret NEO allocation logs as userspace evidence, not kernel copy proof."""
import argparse
from collections import Counter
import json
from pathlib import Path
import re
from analyze_intel_kernel_trace import load_journal

ALLOCATION = re.compile(r"Type: (\w+) Pool: (\w+) Root index: (\d+) Size: (\d+).*?GPU VA: (0x[0-9a-f]+) - .*?Handle: (\d+)")
PRIME = re.compile(r"Created BO-(\d+) range: ([0-9a-f]+) - [0-9a-f]+, size: (\d+) from PRIME_FD_TO_HANDLE")
BIND = re.compile(r"vm=(\d+) obj=(0x[0-9a-f]+) off=0x0 range=(0x[0-9a-f]+) addr=(0x[0-9a-f]+) operation=0\(MAP\).*?ret=(-?\d+)")


def analyze(lines, journal, frames, reuse=False):
    if not isinstance(frames, int) or not 1 <= frames <= 32:
        raise ValueError("Userspace qualification requires 1..32 frames")
    counts, binds, imports = Counter(), Counter(), Counter()
    isa = {}
    markers = []
    phase, frame = "unmarked", None
    for number, line in enumerate(lines, 1):
        if number > 200000:
            raise ValueError("Userspace log exceeds bounded diagnostic size")
        if line.startswith("MKVC_PHASE "):
            entry = json.loads(line[len("MKVC_PHASE "):])
            markers.append(entry)
            phase, frame = entry["phase"], entry.get("frame")
            continue
        # GEM handles/addresses can be reused. Never attach a retired ISA label
        # to a later allocation just because its numeric handle is identical.
        closed = re.search(r"GemClose h=(0x[0-9a-f]+) r=0", line)
        created = re.search(r"DRM_IOCTL_XE_GEM_CREATE has returned: 0 BO-(\d+)", line)
        if closed or created:
            handle = int(closed[1], 16) if closed else int(created[1])
            isa = {key: value for key, value in isa.items() if key[0] != handle}
        allocation = ALLOCATION.search(line)
        if allocation:
            kind, pool, root, size, address, handle = allocation.groups()
            counts[(phase, kind, pool, int(root), int(size))] += 1
            if kind in ("KERNEL_ISA", "KERNEL_ISA_INTERNAL") and root == "0":
                # Tested Arc uses 48-bit GPU VA canonicalization. This is a GPU
                # address/handle pair, never the kernel BO pointer from perf.
                isa[(int(handle), int(address, 16) & ((1 << 48) - 1))] = (kind, int(size))
        elif "ThreadID:" in line and "Type:" in line:
            raise ValueError("Malformed/interleaved allocation record")
        prime = PRIME.search(line)
        if prime:
            imports[(phase, int(prime[3]))] += 1
        bind = BIND.search(line)
        if bind:
            vm, handle, size, address, result = bind.groups()
            key = (int(handle, 16), int(address, 16))
            if key in isa:
                kind, allocation_size = isa[key]
                if int(size, 16) != allocation_size or result != "0":
                    raise ValueError("ISA bind failed or mismatched range")
                binds[(phase, kind)] += 1
    if markers != journal:
        raise ValueError("Driver log and journal markers differ")
    expected = sum(v for k, v in counts.items() if k[1] == "KERNEL_ISA" and k[3] == 0)
    if expected != (2 if reuse else 2 * frames) or not imports or not binds:
        raise ValueError("Missing expected per-frame ISA/shared-image evidence")
    return {
        "version": 1, "status": "observed_partial", "complete_copy_proof": False,
        "opencl_reuse_program": reuse,
        "allocations": [dict(phase=k[0], type=k[1], pool=k[2], root_index=k[3], size=k[4], count=v)
                        for k, v in sorted(counts.items())],
        "isa_gpu_address_handle_matched_binds": [dict(phase=k[0], type=k[1], count=v) for k, v in sorted(binds.items())],
        "shared_imports": [dict(phase=k[0], size=k[1], count=v) for k, v in sorted(imports.items())],
        "limitations": ["Instrumented NEO userspace run, not simultaneous kernel trace",
                        "GPU VA/handle matches are not kernel BO identity or migration byte measurements",
                        "Root index 0 and 48-bit GPU VA are specific to this Arc qualification environment",
                        "Phase delimiters drain C stdio; multithreaded logs can still be ambiguous",
                        "No absence-of-copy claim for internal blits, kernel workers or media-driver internals"],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("journal", type=Path)
    parser.add_argument("workload", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.resolve() in {p.resolve() for p in (args.log, args.journal, args.workload)}:
        parser.error("Do not overwrite input evidence")
    args.output.write_text('{"status":"not_completed"}\n')
    try:
        journal = load_journal(args.journal.read_text().splitlines())
        workload = json.loads(args.workload.read_text())
        if (workload.get("validation") != "passed" or workload.get("pid") != journal[0]["pid"]
                or workload.get("batches") != 1
                or workload.get("gpu_memory", {}).get("processing_device", {}).get("pci") != "0000:83:00.0"):
            raise ValueError("Missing matching successful Arc workload")
        with args.log.open() as stream:
            reuse = workload.get("opencl_reuse_program", False)
            if not isinstance(reuse, bool):
                raise ValueError("Invalid reuse-mode evidence")
            report = analyze(stream, journal, workload["total_frames"], reuse=reuse)
        args.output.write_text(json.dumps(report, indent=2) + "\n")
    except (ValueError, KeyError, TypeError, OSError) as error:
        args.output.write_text(json.dumps({"status": "failed", "error": str(error)}) + "\n")
        parser.exit(1, f"Userspace trace rejected: {error}\n")


if __name__ == "__main__":
    main()
