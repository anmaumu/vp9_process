# Performance benchmarking

`benchmarks/pipeline_benchmark.py` runs an encode/decode round trip exclusively
through the public Python API and writes schema-versioned JSON. It measures total
encode/decode throughput, write-call latency distribution, decode first-frame
latency, encoded size and process peak RSS where available. The record explicitly
states the observed CPU-copy path; it never infers zero-copy from a requested
backend.

The `native_metrics` section is read from the C ABI after close and contains
accepted/completed/rejected frame counts, host queue wait and backend time,
configured/peak queue depth, hardware pending-operation peak and the copy path
actually exercised. Timings are cumulative monotonic host-clock nanoseconds.

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

This runner records end-to-end host timings plus the current native aggregate
queue/backend timings. Conversion, codec, mux and GPU-event timers are not yet
separated and must not be reverse-engineered from the aggregate totals.

## Packed BGR qualification observation

The 2026-09-08 Windows qualification used a 600-frame, 1920x1080, 60 fps VP9
test pattern, one full warm-up read and five timed full reads including
open/close. The host was a 12-core/24-thread Xeon E5-2697 v2 with an RTX 2060.

| Python path | Before parallel conversion | After | Median speedup |
|---|---:|---:|---:|
| CPU `read_bgr()`, default decode threads | 57.6 fps | 121.6 fps | 2.11x |
| CPU `read_bgr()`, 16 decode threads | 62.0 fps | 204.8 fps | 3.30x |
| NVIDIA `read_bgr()` | 54.5 fps | 179.9 fps | 3.30x |

OpenCV 5.0 using its FFmpeg backend and 16 reported decode threads measured
136.5 fps on the same file. This comparison validates the optimization on one
machine only; it is neither a cross-platform baseline nor evidence about GPU
surface processing. The pixel regression compares conversion output against
the unpartitioned libyuv function rather than accepting throughput alone.
