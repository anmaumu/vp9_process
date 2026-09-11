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
actually exercised. Its `encoder_stages` and `decoder_stages` records split
frame calls from flush/finalization and distinguish caller-thread work from
queue/prefetch-worker work. Timings are cumulative monotonic host-clock
nanoseconds. They describe host API boundaries and do not claim driver-kernel or
device-event duration.

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
hardware-class baselines exist. Once a result is approved, add
`"performance_gate": {"max_regression_fraction": 0.10}` to that baseline and run:

```shell
python tools/check_performance_baseline.py \
  --baseline benchmark-results/approved.json \
  --candidate benchmark-results/candidate.json
```

The gate fails closed for mismatched cases, missing/non-finite values, or a
throughput/latency regression beyond the reviewed fraction. Baselines must be
kept per hardware/driver/build class; the tool deliberately does not normalize
results from unlike machines.

The operation metrics do not separate conversion, codec, mux or GPU-event time;
those values must not be reverse-engineered from host-boundary totals.

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

## Classic MSVC x64 24-bit qualification

The 2026-09-09 qualification used the same Windows host and 600-frame 1080p60
VP9 fixture, with codec threads fixed to one and read-ahead disabled. Classic
MSVC x64 uses libyuv's SIMD-enabled I420-to-four-channel conversion followed by
a Highway runtime-dispatched alpha-removal pack. Other compiler/platform builds
retain libyuv's direct one-pass 24-bit conversion.

| Python path | Direct libyuv 24-bit | libyuv + Highway | Median speedup |
|---|---:|---:|---:|
| Retained-frame BGR conversion, 1 thread | 70.2 fps | 434.7 fps | 6.19x |
| Full `read_bgr()`, 1 decode/1 conversion thread, no prefetch | 38.0 fps | 72.35 fps | 1.90x |
| Full `read_bgr()`, 1 decode/4 conversion threads, no prefetch | 75.2 fps | 84.57 fps | 1.12x |

Each new value is the median of five runs after warm-up. Output bytes and
checksums match the direct libyuv BGR/RGB reference, including non-vector-width
tails and padded destination rows. These measurements qualify this machine and
are not cross-platform release guarantees.
