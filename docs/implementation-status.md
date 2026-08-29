# Implementation status

この文書は仕様の正本を変更せず、実装・検証の到達状況を記録する。

## Documentation tooling

- `docgen check/generate/build` validates specification IDs and generates Markdown/HTML.
- C ABI and Python API references are extracted from source declarations.
- Doxygen generates C/C++ HTML and XML from public/internal source comments.
- Every exported `MKVC_API` symbol must have a Doxygen comment; missing comments fail docgen.
- GitHub Actions builds strict MkDocs HTML and stores `mkvcodec-documentation` for 30 days.
- GitHub Pages publication remains disabled until an explicit public-release decision.

## 2026-08-29: CPU VP9/AV1 and Intel writer slice

Status: `PARTIAL`

| Specification | Implementation | Verification | Status |
|---|---|---|---|
| `EXT-CODEC-001` / `AC-CODEC-001` | libvpx VP9 CPU encode/decode | `mkvc_cpu_vp9_encode` | synchronous I420 round-trip passing with PSNR >= 28 dB |
| `EXT-CODEC-002` / `AC-CODEC-002` | SVT-AV1 encode and libaom decode | `mkvc_python_av1_encode` | 8-bit internal round-trip, FFmpeg decode and Y-PSNR >= 28 dB passing; SSIM pending |
| `EXT-CONT-001..003` / `AC-CONT-001` | libwebm WebM mux/finalize/demux | `mkvc_cpu_vp9_external_decode`, `mkvc_cpu_vp9_metadata` | WebM VP9 path complete; MKV distinction pending |
| `EXT-ENC-001` | create/write/flush/idempotent close/destroy | `mkvc_cpu_vp9_encode` | synchronous and bounded asynchronous CPU paths complete |
| `EXT-ENC-002` | BGR/RGB/BGRA/I420/NV12 CPU input | VP9/AV1 Python round-trips | complete for both CPU writers |
| `EXT-ENC-005` | asynchronous input deep-copied before return | mutable reused inputs in native/Python round-trip | complete for supported CPU formats |
| `EXT-ENC-006` | bounded queue/pool; blocking write; nonblocking try-write; ordered flush/close | native and Python round-trip | complete for CPU writer; cancel API pending |
| `EXT-ENC-007` | CQ quality 0..63, default contract 32 | integration config uses 32 | backend mapping complete; binding default pending |
| `EXT-ENC-009` | four-second keyframe default, auto threads | code review/build | complete for libvpx writer |
| `EXT-DEC-001/005` | create/read/EOS/idempotent close/destroy | `mkvc_cpu_vp9_encode` | synchronous C ABI subset complete |
| `EXT-DEC-001/005` Python | context manager, BGR iterator, `None`/StopIteration EOS | `mkvc_python_roundtrip` | synchronous CPU subset complete |
| `EXT-DEC-002` | `read_bgr`, `read_i420`, `read_nv12` plus RGB/BGRA | VP9/AV1 Python round-trips | CPU outputs complete |
| `EXT-DEC-004` | `prefetch=0` synchronous and positive bounded native read-ahead | VP9/AV1 round-trips and early close | CPU decoders complete |
| `EXT-ENC-001/002/005/006` Python | context manager; BGR default; safe input; queue_size; try_write | `mkvc_python_roundtrip` | CPU input and bounded async complete |
| `INT-CPU-002` | libyuv BGR/RGB/BGRA/NV12 conversion | known-color and padded-stride round-trip | complete for 8-bit formats |
| `TEST-ENC-001` | dtype/shape/positive and negative stride validation | `mkvc_python_roundtrip` | supported CPU input formats passing |
| `EXT-FRAME-001` | owned I420 planes, stride, dimensions and PTS | round-trip frame assertions | I420 frame complete |
| `EXT-ABI-002..005` | `mkvc_`, opaque encoder handle, versioned structs, stable result | `mkvc_c_api_tests` | encoder subset complete |
| `EXT-ERR-002..003` | exception containment and thread-local detail | C ABI tests/integration | encoder subset complete |
| `INT-CPU-001` | libvpx VP9 encode/decode | Linux GCC build and round-trip | VP9 synchronous subset complete |
| `INT-CONT-001/003` | libwebm mux/demux, `V_VP9`, PTS/duration/keyframe | FFmpeg/ffprobe tests | WebM VP9 subset complete |
| `INT-CPU-004` / `INT-PIPE-001/005/006` | bounded worker queue, reusable frame buffers, wakeups, owned input | native/Python nonblocking and flush tests | CPU writer complete except explicit cancel API/metrics |
| `INT-STATE-001..003` | running/flushing/closed behavior | close/write-after-close checks | CPU writer subset complete |
| `TEST-CONT-001` | independent decode and metadata verification | FFmpeg + ffprobe | VP9 WebM encode case passing |
| `TEST-CODEC-001` | VP9 encode/decode round-trip with quality metrics | internal decode and Y-PSNR >= 28 dB | SSIM pending |
| `TEST-CODEC-002` | SVT-AV1 to libaom/FFmpeg round-trip | 30-frame PTS/order/count, Y-PSNR >= 28 dB, all 8-bit inputs | SSIM pending |
| `INT-INTEL-001/002` | oneVPL 2.x hardware session, internal NV12 surfaces, VP9/AV1 encode/decode and libwebm mux/demux | `mkvc_intel_vpl_probe`, `mkvc_intel_vpl_encode`, `mkvc_python_intel_roundtrip` in required-hardware mode | Linux VA-API public Writer/Capture passing for both codecs; sync/prefetch decode and all five writer inputs complete; multi-SyncPoint overlap, zero-copy and Windows hardware run pending |
| `EXT-BACK-001` Intel | runtime capability exposes each Query-supported encode/decode direction | C ABI capability assertions with real hardware and oneVPL-disabled builds | no false direction advertisement; unavailable build omits Intel rows |
| `EXT-CS-001/002`, `INT-CS-001` | .NET 8 P/Invoke types, typed exception and encoder/decoder/frame SafeHandle ownership | `mkvc_dotnet_build`, `mkvc_dotnet_smoke` | Linux ABI layout, native load, version and capability query passing; high-level IDisposable reader/writer and NuGet pending |

The decoder keeps a libwebm cluster/block/frame cursor and reads compressed packets
incrementally. One compressed packet is limited to 256 MiB. No CPU pipeline uses
unbounded storage: positive encoder `queue_size` and decoder `prefetch` values select
fixed-capacity native worker queues. Encoder `flush` is an ordered barrier and `close`
drains accepted work before finalizing the container.
SVT-AV1 flush ends and drains the current codec sequence, recreates the encoder,
and permits subsequent frames in the same WebM track; this preserves the public
ordered-flush-and-continue contract despite SVT-AV1 lookahead.
The Intel writer follows the same flush-and-continue contract by draining and
recreating its oneVPL codec adapter while retaining monotonically increasing PTS.
The Intel decoder incrementally demuxes WebM packets, maps oneVPL NV12 output,
copies it into the common owned I420 frame, and participates in the same bounded
native prefetch queue as CPU decode.
The current oneVPL adapter sets `AsyncDepth=4`, but synchronizes each returned
SyncPoint immediately; actual multi-frame GPU overlap remains a measured follow-up.

## Verified dependency baseline

- vcpkg registry baseline `114d9fe62faf35856b45cf55cb93b57028a45d63`
- libvpx `1.16.0#3`
- libwebm `1.0.0.32`
- libyuv `1916`
- SVT-AV1 `4.1.0`
- libaom `3.15.0`
- oneVPL dispatcher `2.17.0`; Intel GPU runtime API `2.15`
- Linux test host: `linux-machine`, GCC 13.3, CMake 3.28, Ninja
