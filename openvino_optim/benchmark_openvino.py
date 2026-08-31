import argparse
import ctypes
import csv
import json
import os
import platform
import statistics
import time
from pathlib import Path

import numpy as np
import openvino as ov


def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument("--models", nargs="+", required=True)
    p.add_argument("--output", required=True)
    p.add_argument("--iterations", type=int, default=40)
    p.add_argument("--warmup", type=int, default=8)
    p.add_argument("--label", required=True)
    p.add_argument("--single-core", action="store_true", help="Restrict the process to logical CPU 0 and benchmark one OpenVINO thread")
    return p.parse_args()


def configs():
    # Comparable latency-oriented configurations. 24 threads equals the physical
    # core count of this dual-socket host; 12 confines work to one socket's cores.
    return [
        ("default", {}),
        ("latency", {"PERFORMANCE_HINT": "LATENCY"}),
        ("pin_on", {"PERFORMANCE_HINT": "LATENCY", "ENABLE_CPU_PINNING": True}),
        ("pin_on_24t", {"PERFORMANCE_HINT": "LATENCY", "ENABLE_CPU_PINNING": True, "INFERENCE_NUM_THREADS": 24}),
        ("pin_on_12t", {"PERFORMANCE_HINT": "LATENCY", "ENABLE_CPU_PINNING": True, "INFERENCE_NUM_THREADS": 12}),
        ("pin_off_24t", {"PERFORMANCE_HINT": "LATENCY", "ENABLE_CPU_PINNING": False, "INFERENCE_NUM_THREADS": 24}),
    ]


def pin_process_to_cpu0():
    if os.name == "nt":
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel32.GetCurrentProcess.restype = ctypes.c_void_p
        kernel32.SetProcessAffinityMask.argtypes = (ctypes.c_void_p, ctypes.c_size_t)
        kernel32.SetProcessAffinityMask.restype = ctypes.c_int
        handle = kernel32.GetCurrentProcess()
        if not kernel32.SetProcessAffinityMask(handle, 1):
            raise ctypes.WinError(ctypes.get_last_error())
    else:
        os.sched_setaffinity(0, {0})


def main():
    args = parse_args()
    if args.single_core:
        pin_process_to_cpu0()
    core = ov.Core()
    rows = []
    rng = np.random.default_rng(20260901)
    for model_path in args.models:
        model = core.read_model(model_path)
        partial = model.input(0).partial_shape
        shape = [1 if dim.is_dynamic else dim.get_length() for dim in partial]
        if partial.is_dynamic:
            model.reshape({model.input(0): shape})
        data = rng.random(shape, dtype=np.float32)
        test_configs = [("1thread_cpu0", {
            "PERFORMANCE_HINT": "LATENCY",
            "INFERENCE_NUM_THREADS": 1,
            "ENABLE_CPU_PINNING": True,
            "ENABLE_HYPER_THREADING": False,
        })] if args.single_core else configs()
        for config_name, config in test_configs:
            try:
                compiled = core.compile_model(model, "CPU", config)
                req = compiled.create_infer_request()
                for _ in range(args.warmup):
                    req.infer({0: data})
                samples = []
                wall0 = time.perf_counter()
                cpu0 = time.process_time()
                for _ in range(args.iterations):
                    t0 = time.perf_counter_ns()
                    req.infer({0: data})
                    samples.append((time.perf_counter_ns() - t0) / 1e6)
                cpu_s = time.process_time() - cpu0
                wall_s = time.perf_counter() - wall0
                samples.sort()
                props = {}
                for key in ("INFERENCE_NUM_THREADS", "NUM_STREAMS", "PERFORMANCE_HINT", "ENABLE_CPU_PINNING", "ENABLE_HYPER_THREADING"):
                    try:
                        props[key] = str(compiled.get_property(key))
                    except Exception:
                        pass
                rows.append({
                    "environment": args.label,
                    "model": Path(model_path).stem,
                    "config": config_name,
                    "mean_ms": round(statistics.mean(samples), 3),
                    "median_ms": round(statistics.median(samples), 3),
                    "p90_ms": round(samples[int(0.9 * (len(samples) - 1))], 3),
                    "min_ms": round(min(samples), 3),
                    "fps": round(args.iterations / wall_s, 3),
                    "process_cpu_per_wall": round(cpu_s / wall_s, 3),
                    "effective_properties": json.dumps(props, sort_keys=True),
                    "openvino": ov.__version__,
                    "python": platform.python_version(),
                    "platform": platform.platform(),
                    "logical_cpus": os.cpu_count(),
                })
                print(json.dumps(rows[-1], ensure_ascii=False), flush=True)
            except Exception as exc:
                rows.append({"environment": args.label, "model": Path(model_path).stem, "config": config_name, "error": repr(exc)})
                print(json.dumps(rows[-1], ensure_ascii=False), flush=True)
    fields = sorted({k for row in rows for k in row})
    with open(args.output, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


if __name__ == "__main__":
    main()
