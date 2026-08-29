# Performance benchmarking

`benchmarks/pipeline_benchmark.py` runs an encode/decode round trip exclusively
through the public Python API and writes schema-versioned JSON. It measures total
encode/decode throughput, write-call latency distribution, decode first-frame
latency, encoded size and process peak RSS where available. The record explicitly
states the observed CPU-copy path; it never infers zero-copy from a requested
backend.

Example:

```shell
python benchmarks/pipeline_benchmark.py \
  --backend intel --codec av1 --width 1920 --height 1080 \
  --frames 300 --fps 60 --queue-size 8 --prefetch 4 \
  --output benchmark-results/intel-av1-1080p60.json
```

Run separate records for 1080p30, 1080p60, 4K30 and hardware-supported 4K60.
Keep the native library, driver, CPU/GPU model, power policy and build type stable
when comparing results. Absolute release thresholds remain unset until approved
hardware-class baselines exist.

This runner records end-to-end host timings. Fine-grained queue wait, conversion,
codec, mux and GPU-event timings require the planned native metrics ABI and must
not be reverse-engineered from these totals.
