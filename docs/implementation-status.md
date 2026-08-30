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

Frame processing status: the common immutable C ABI and CPU/libyuv implementation
now cover resize, crop, 8-bit I420-to-basic-format conversion, rotate/flip, and
letterbox/pillarbox (`contain`/`cover`). Python exposes the same native plan through
`VideoCapture.read_processed`. NVIDIA NPP/CUDA, Intel oneVPL VPP/shared-surface,
color-metadata conversion, surface pooling/fusion, and C# bindings remain future
work; GPU requests are rejected instead of silently falling back to CPU.

GPU-resident implementation contract is now specified by `EXT-GPU-001..010`,
`INT-GPU-001..014`, `AC-GPU-001`, and `TEST-GPU-001..014`. The associated
ownership, synchronization, DLPack, device-loss, hidden-copy, and pool-exhaustion
risks are tracked in `gpu-risk-register.md`; these entries describe planned work,
not current implementation.

The first GPU ownership foundation is implemented under `src/gpu/`: opaque
`mkvc_gpu_frame`, immutable descriptor, retain/release lease, generation,
producer completion query/wait/timeout/failure, internal consumer completion,
and recycle gating across producer + consumers + external lease count. The
deterministic `mkvc_gpu_frame` test covers the core of `TEST-GPU-001/002/012`;
backend surface factories, real GPU completion adapters and hardware traces are
still pending.

The common native export ABI now describes borrowed D3D11 texture/subresource,
VA display/surface, CUDA pointer/CUarray/context/stream/event, and USM resources
without exposing vendor SDK types. Intel and NVIDIA descriptor factories validate
the platform handle layout and bind device identity + pool generation to the
lease. Connection to real decoded surfaces remains pending, so this is only the
contract/factory subset of `TEST-GPU-003/005`.

Backend completion foundations now normalize oneVPL `mfxSyncPoint` and NVIDIA
CUDA event query into the common non-device-wide `Completion` interface. The
polling adapter provides bounded timeout and deterministic pending/complete/error
tests. These adapters compile in oneVPL-enabled and NVIDIA-enabled strict builds;
they are not yet wired into decoded GPU frame factories, so hardware completion
trace qualification remains pending.

`GpuFramePool` now enforces a fixed slot capacity, monotonically increasing slot
generation, `WOULD_BLOCK` backpressure, peak usage metrics and generation-matched
single recycle. Producer completion alone cannot recycle a slot while an external
lease or consumer completion remains. This is the deterministic foundation for
`TEST-GPU-001/002/014`; VRAM allocation and long hardware soak remain pending.

The Intel GPU surface factory can now wrap an NV12/P010 oneVPL video-memory
surface plus SyncPoint into `GpuFramePool` without CPU Map, export its D3D11 or
VA native descriptor, and defer `FrameInterface::Release` until completion and
all leases finish. Abandoned internal frames perform a best-effort completion
wait before releasing the backend resource. The factory is now consumed by the
Linux Intel public GPU decode path; D3D11 and VPP/encode consumers remain incomplete.

Linux Intel public GPU decode is now connected end to end: the decoder requests
`MFX_IOPATTERN_OUT_VIDEO_MEMORY`, `mkvc_decoder_read_gpu` returns a leased VA
surface, and Python exposes it as `VideoCapture.read_surface()`. The frame retains
the oneVPL session after Capture close, reports `zero_copy`, and releases the VA
surface/session at final lease release. VP9 native and Python hardware tests pass
on `linux-machine`; AV1 Python GPU surface acquisition also passes. VPP→encode
connection and Windows D3D11 hardware qualification remain pending.

Linux Intel VP9 decode surfaces can now be submitted through
`mkvc_encoder_write_gpu_frame` / Python `VideoWriter.write_surface`. The internal
oneVPL surface is retained by the common lease, encode completion is registered as
a consumer dependency, and the VA surface is never CPU-mapped or read back. A
12-frame 320x240 VP9 and AV1 decode→encode→decode tests pass on `linux-machine`, including
immediate Python lease release after submission. The current implementation waits
the producer and oldest encode SyncPoint on the calling thread for bounded pool
progress; VPP, asynchronous cross-stage overlap, Windows D3D11, and trace-based
proof of zero CPU transfer remain pending.
Encoder metrics now distinguish CPU-only, GPU-resident, and mixed input paths.
Python Writer/Capture accept `require_gpu_resident=True`; CPU submission/read APIs
then fail instead of silently crossing host memory. The first supported strict
combination is Intel Capture with `prefetch=0` and Intel Writer with `queue_size=0`.
The same contract is exposed to C/C++ and future C# bindings through versioned
`mkvc_copy_policy` encoder/decoder setters without changing existing create-config
structure sizes.

`benchmarks/gpu_transcode_benchmark.py` measures the strict public surface path.
An initial Linux Intel VP9 run at 1920x1080, 120 frames reported 169.6 fps total,
3.52 ms mean submission latency, 4.29 ms p95, and `zero_copy` from both native
stages. This is a development baseline rather than an acceptance threshold: input
was cached, initialization/finalization are included, the path is host-synchronous,
and an OS/oneVPL trace has not yet independently proven zero host pixel transfer.

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
| `EXT-PROC-002..006` / `INT-PROC-001,005` | immutable CPU process plan: crop, bilinear resize, rotate/flip, contain/cover composition and basic output conversion | `mkvc_frame_processor`, `mkvc_python_roundtrip` | CPU 8-bit subset complete; GPU and color metadata pending |
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
| `INT-INTEL-001/002` | oneVPL 2.x hardware session, internal NV12 surfaces, VP9/AV1 encode/decode and libwebm mux/demux | `mkvc_intel_vpl_probe`, `mkvc_intel_vpl_encode`, `mkvc_python_intel_roundtrip` in required-hardware mode | Linux VA-API public Writer/Capture passing for both codecs with ordered multi-SyncPoint encode/decode; zero-copy and Windows hardware run pending |
| `INT-PIPE-003`, `TEST-INTEL-002` | per-operation bitstream/surface ownership and oldest-first SyncPoint collection | VP9/AV1 encode and decode at AsyncDepth 1/2/4/8 plus injected collection failure | exact requested pending high-water mark, ordered output, best-effort SyncPoint cleanup, repeated idempotent close and post-failure session recreation passing |
| `INT-ERR-006`, `INT-PIPE-005`, `TEST-ERR-002` | test-build-only asynchronous backend failure injection | `mkvc_async_failure` with eight concurrent writers and queue capacity one | all blocked writers wake within timeout, terminal IO reaches close, queue stays bounded and a clean session can be recreated |
| `EXT-BACK-001` Intel | runtime capability exposes each Query-supported encode/decode direction | C ABI capability assertions with real hardware and oneVPL-disabled builds | no false direction advertisement; unavailable build omits Intel rows |
| `EXT-CS-001/002`, `INT-CS-001`, `TEST-CS-001` | .NET 8 P/Invoke types, typed exception, CPU/GPU frame SafeHandle ownership, IDisposable writer/capture, owned I420 result and leased `ReadSurface` result with descriptor/native-handle/wait access | `mkvc_dotnet_build`, `mkvc_dotnet_smoke`; GPU descriptor/native-handle ABI sizes asserted as 136/64 bytes on 64-bit targets | Linux CPU ABI/native-load and bounded writer → prefetch capture 10-frame ordered round-trip previously passed. GPU source API is implemented; the current Windows and Linux hosts lack a .NET SDK, so managed compilation and Intel/NVIDIA hardware smoke for this addition remain pending. Packed CPU formats, async Task API and NuGet publication are also pending |
| `INT-PERF-001`, `TEST-PERF-001/002` foundation | versioned public-API JSON benchmark parameterized by backend/codec/resolution/fps/queue/prefetch | `mkvc_python_benchmark_smoke` | end-to-end fps, submit latency distribution, first-frame latency, bytes, RSS and explicit copy path recorded; approved baselines/regression thresholds pending |
| `EXT-OBS-001`, `INT-OBS-001/003/004` aggregate subset | versioned C ABI/Python metrics snapshots for frame counts, queue wait, backend time, capacity/peak, GPU pending peak and actual copy path | CPU native round-trip, Python benchmark smoke and Intel public round-trip | bounded queue/high-water and four pending Intel operations observed; per-stage conversion/codec/mux and GPU-event timing pending |
| `INT-NV-001..003`, `TEST-NV-001`, `TEST-BACK-001`, `TEST-GPU-005` decode slice | runtime-only CUDA/NVCUVID loading, incremental libwebm demux, CPU I420 readback mode, and GPU mode returning leased mapped NVDEC CUDA pointers through the common C ABI/Python surface API | `mkvc_nvidia_probe`, `mkvc_nvidia_webm_decode` on Windows plus NVIDIA-free Linux strict build | RTX 2060 VP9 produces 30 ordered CUDA NV12 frames with pointer/context/pitch/plane offsets; holding the first lease does not drop frames; Capture close defers unmap/context destruction until final release. AV1 positive hardware, CUarray/event/stream export and trace proof remain pending |
| `INT-NV-001..006`, `TEST-NV-002..005`, `TEST-GPU-007` encode/transcode slice | runtime-loaded CUDA/NVENC AV1 P4 synchronous adapter; CPU inputs plus leased NVDEC CUDA-pointer NV12 registration/map/encode/unmap/unregister; same-context enforcement, nanosecond PTS propagation and libwebm mux through the common Writer | `mkvc_nvidia_webm_encode`, `mkvc_nvidia_gpu_transcode`, NVIDIA-enabled Windows build and strict Linux build | VP9 encode is always rejected; unsupported AV1 hardware and NVIDIA-free hosts skip/fail without output. Source implementation and lifetime cleanup are present; RTX 2060 passes NVDEC regressions but cannot encode AV1, so positive registered-resource encode, independent golden decode and DtoH/HtoD trace proof remain pending on an AV1-capable GPU |
| `EXT-GPU-008`, `INT-GPU-010/011`, `TEST-GPU-009/010` DLPack foundation | C ABI exports each linear CUDA NV12 plane as a standard uint8 `DLManagedTensor`; native deleter retains the GPU-frame lease independently of Python GC; texture/VA/nonlinear and unsupported event dependencies fail closed | `mkvc_gpu_frame` ABI test validates Y/UV pointer offset, shape, byte strides, CUDA device identity and source-release-before-consumer-deleter ordering | Native CUDA-pointer adapter complete. Python capsule/CuPy integration, consumer-stream CUDA event insertion, subprocess shutdown stress and Intel USM remain pending |
| `EXT-PKG-001..004`, `AC-PKG-001`, `TEST-PKG-001..004` foundation | versioned dependency manifest, SPDX 2.3 generator, source policy scan and wheel/NuGet/ZIP content inspector | `mkvc_compliance_source`, `mkvc_compliance_unit`, documentation CI | manifest coverage, H.264/HEVC symbol exclusion, fail-closed legal-file checks and vendor GPU driver exclusion pass; real package construction, original LICENSE/PATENTS collection and final legal approval remain pending |
| `EXT-PKG-002..004`, `TEST-PKG-001/003` legal-source collection | SHA-256 locked collector for libvpx/libwebm/libyuv/libaom/SVT-AV1/libvpl source texts, deduplicated nv-codec-headers MIT blocks and generated notice index | `tests/test_collect_licenses.py`, collection against the pinned Linux vcpkg/NVIDIA header trees | 13 legal payload files reproduced from exact build sources; hash drift and ambiguous source matches fail closed; project license decision and real wheel/NuGet embedding remain pending |
| `EXT-PKG-001..004`, `AC-PKG-001`, `TEST-PKG-001/002` artifact assembly | platform wheel builder with RECORD and package-local native loader; RID-specific NuGet pack inputs with managed/native assets; both embed project-license input, legal payload and SPDX | `tests/test_build_wheel.py`, `tests/test_build_nuget.py`, real Linux wheel install/native load and local-source NuGet restore/full .NET round-trip | Linux wheel and NuGet artifacts pass content gate and execute without `MKVC_LIBRARY_PATH`; project license is mandatory and was represented only by a disposable test fixture; Windows artifacts and publication remain pending |

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
The NVIDIA decoder follows the same bounded prefetch contract. It pushes its
CUDA context on the actual read thread, lets NVCUVID synchronously parse/decode,
maps NV12 only for completed display-order frames, and splits the host readback
into the common owned I420 representation. The NVIDIA writer converts supported
8-bit CPU inputs to NV12, submits one synchronous NVENC AV1 operation at a time,
and immediately muxes the returned packet. Backend capability rows are emitted
only for runtime-supported directions; NVENC AV1 is advertised only when the
runtime encode GUID query succeeds, while NVENC VP9 is never advertised.
The oneVPL encoder and decoder use `AsyncDepth=4` publicly and retain four
independently owned SyncPoint slots before waiting on the oldest operation. Raw
hardware tests also cover depths 1, 2, and 8 for both directions.
Test hooks are compiled only when `MKVC_BUILD_TESTS` is enabled and are not part of
the C ABI. Intel fault tests synchronize or best-effort retire every outstanding
operation before releasing its bitstream/surface and closing the oneVPL session.

## Verified dependency baseline

- vcpkg registry baseline `114d9fe62faf35856b45cf55cb93b57028a45d63`
- libvpx `1.16.0#3`
- libwebm `1.0.0.32`
- libyuv `1916`
- SVT-AV1 `4.1.0`
- libaom `3.15.0`
- oneVPL dispatcher `2.17.0`; Intel GPU runtime API `2.15`
- nv-codec-headers `n13.1.15.0`, source archive SHA-256 `2255bc74d038b95aa4be30f5f66322c2176acbdb90ada1851db6993536fbeaf7`
- Windows NVIDIA probe host: GeForce RTX 2060, compute capability 7.5, CUDA driver API 13.3, NVENC API 13.1
- Linux test host: `linux-machine`, GCC 13.3, CMake 3.28, Ninja
