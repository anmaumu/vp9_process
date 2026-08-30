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

CPU convenience processing status: the common immutable C ABI and CPU/libyuv
implementation covers resize, crop, 8-bit I420-to-basic-format conversion,
rotate/flip, and letterbox/pillarbox (`contain`/`cover`). Python exposes the same
native plan through `VideoCapture.read_processed`. GPU image-processing algorithms
are intentionally outside this library: GPU requests are rejected instead of
silently falling back, and applications use native handle/DLPack interop.

GPU-resident implementation contract is specified as decode/export/external
processing/import/encode by `EXT-GPU-001..010`, `INT-GPU-001..017`,
`AC-GPU-001`, and `TEST-GPU-001..020`. The associated
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
contract/factory subset of `TEST-GPU-003/005`. External processed-resource import
and release-callback ownership are specified but not implemented yet.

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
Linux Intel public GPU decode path; D3D11 export and external-resource import remain incomplete.

Linux Intel public GPU decode is now connected end to end: the decoder requests
`MFX_IOPATTERN_OUT_VIDEO_MEMORY`, `mkvc_decoder_read_gpu` returns a leased VA
surface, and Python exposes it as `VideoCapture.read_surface()`. The frame retains
the oneVPL session after Capture close, reports `zero_copy`, and releases the VA
surface/session at final lease release. VP9 native and Python hardware tests pass
on `linux-machine`; AV1 Python GPU surface acquisition also passes. External
resource import and Windows D3D11 hardware qualification remain pending.

Linux Intel VP9 decode surfaces can now be submitted through
`mkvc_encoder_write_gpu_frame` / Python `VideoWriter.write_surface`. The internal
oneVPL surface is retained by the common lease, encode completion is registered as
a consumer dependency, and the VA surface is never CPU-mapped or read back. A
12-frame 320x240 VP9 and AV1 decode→encode→decode tests pass on `linux-machine`, including
immediate Python lease release after submission. The current implementation waits
the producer and oldest encode SyncPoint on the calling thread for bounded pool
progress; external processed-resource import, asynchronous cross-stage overlap,
Windows D3D11, and trace-based
proof of zero CPU transfer remain pending.
The stable C ABI can now import process-local external GPU resources into the
same `mkvc_gpu_frame` lease model. It validates backend/device/generation and
NV12 pitch/offset layout, polls an explicit producer callback, and invokes the
external release callback exactly once after producer completion and final
lease release. External CUDA-pointer NV12 frames satisfy the existing NVIDIA
encoder contract; mock completion/lifetime tests pass without GPU hardware.
D3D11/VA handles can be wrapped/exported, but direct oneVPL encoding still
requires a native-surface import adapter. .NET `MkvGpuFrame.ImportExternal`
roots its managed owner through final native release and safely bridges
thread-safe producer/release callbacks. Python uses a stable-ABI native holder
so the owner survives wrapper GC without a Python finalizer callback. The adapter
accepts explicitly synchronized or producer-CUDA-event-backed contiguous CUDA
pointer NV12 frames. Native event import dynamically loads the CUDA driver,
pushes the supplied context only around `cuEventQuery`, and never performs a
device-wide synchronize. A Windows NVIDIA hardware test validates context/event
completion and exactly-once release. CUarray, DLPack import and CuPy end-to-end
hardware qualification remain pending.
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
| `EXT-DEC-006`, `EXT-ENC-011`, `EXT-FRAME-006..007` | retained native I420 descriptor, Python read-only borrowed NumPy views, synchronous borrowed encode | native VP9 and `mkvc_python_roundtrip` lifetime/round-trip coverage | initial C ABI/Python slice complete; borrowed encode currently requires `queue_size=0` |
| `EXT-ENC-012`, `EXT-FRAME-009` | C ABI/Python async borrowed submission with query/wait/release and owner retention | native failure injection and Python GC/round-trip coverage | initial implementation complete; input mutation is prohibited until terminal completion |
| `EXT-FRAME-011` | `WriteBorrowedI420` short-duration managed pin plus `MkvCpuFramePool` writable unmanaged spans and `MkvSubmission` completion lease | `.NET` native-load, borrowed and pooled async round-trip smoke | synchronous short-pin and asynchronous unmanaged pool complete; optional OS page-lock metrics remain pending |
| `EXT-ENC-013`, `EXT-FRAME-010..012` native-pool subset | fixed-capacity C ABI pool, generation-checked slot lease, nonblocking/timed backpressure, Python NumPy/.NET Span views, async encoder ownership transfer | `mkvc_cpu_frame_pool`, Python/.NET capacity/generation/view-lifetime/round-trip coverage | native allocation slice complete; OS page-lock, strict fallback and detailed copy trace remain pending |
| C++ RAII facade | header-only move-only `Encoder`, `Decoder`, CPU/GPU `Frame`, `CpuFramePool`, `CpuBuffer`, `Submission` over the stable C ABI; typed `ResultError` retains the native result | `mkvc_cpp_raii` move/lifetime/generation/async encode/decode round-trip | common CPU path complete and GPU source/sink facade compile-qualified; Intel/NVIDIA C++ hardware round-trip remains pending |
| `EXT-GPU-004..005`, `INT-GPU-008/010` external-frame subset | `mkvc_gpu_frame_import_external` and `mkvc_gpu_frame_import_cuda_event`, C++ RAII factories, .NET managed-owner adapters and Python stable-ABI synchronized/event pointer adapter; callback or native CUDA-event producer completion, immutable descriptor/native handle, single-shot release and CUDA-pointer backend binding | native/C++/.NET/Python callback lifecycle tests plus Windows CUDA context/event hardware test | linear CUDA-pointer import and native event dependency complete; DLPack/CUarray and Intel oneVPL external-resource import pending |
| `EXT-ENC-006` | bounded queue/pool; blocking write; nonblocking try-write; ordered flush/close; explicit cancel wakeup | native async failure/cancel and Python round-trip | complete for CPU writer; queued submissions receive a distinct cancelled terminal state while an already-active codec call finishes safely |
| `EXT-ENC-007` | CQ quality 0..63, default contract 32 | integration config uses 32 | backend mapping complete; binding default pending |
| `EXT-ENC-009` | four-second keyframe default, auto threads | code review/build | complete for libvpx writer |
| `EXT-DEC-001/005` | create/read/EOS/idempotent close/destroy | `mkvc_cpu_vp9_encode` | synchronous C ABI subset complete |
| `EXT-DEC-001/005` Python | context manager, BGR iterator, `None`/StopIteration EOS | `mkvc_python_roundtrip` | synchronous CPU subset complete |
| `EXT-DEC-002` | `read_bgr`, `read_i420`, `read_nv12` plus RGB/BGRA | VP9/AV1 Python round-trips | CPU outputs complete |
| `EXT-DEC-004` | `prefetch=0` synchronous and positive bounded native read-ahead | VP9/AV1 round-trips and early close | CPU decoders complete |
| `EXT-ENC-001/002/005/006` Python | context manager; BGR default; safe input; queue_size; try_write | `mkvc_python_roundtrip` | CPU input and bounded async complete |
| `INT-CPU-002` | libyuv BGR/RGB/BGRA/NV12 conversion | known-color and padded-stride round-trip | complete for 8-bit formats |
| `EXT-PROC-002..006` / `INT-PROC-001,005` | immutable CPU process plan: crop, bilinear resize, rotate/flip, contain/cover composition and basic output conversion | `mkvc_frame_processor`, `mkvc_python_roundtrip` | CPU 8-bit subset complete; GPU processing is out of scope; color metadata pending |
| `TEST-ENC-001` | dtype/shape/positive and negative stride validation | `mkvc_python_roundtrip` | supported CPU input formats passing |
| `EXT-FRAME-001` | owned I420 planes, stride, dimensions and PTS | round-trip frame assertions | I420 frame complete |
| `EXT-ABI-002..005` | `mkvc_`, opaque encoder handle, versioned structs, stable result | `mkvc_c_api_tests` | encoder subset complete |
| `EXT-ERR-002..003` | exception containment and thread-local detail | C ABI tests/integration | encoder subset complete |
| `INT-CPU-001` | libvpx VP9 encode/decode | Linux GCC build and round-trip | VP9 synchronous subset complete |
| `INT-CONT-001/003` | libwebm mux/demux, `V_VP9`, PTS/duration/keyframe | FFmpeg/ffprobe tests | WebM VP9 subset complete |
| `INT-CPU-004` / `INT-PIPE-001/005/006` | bounded worker queue, reusable frame buffers, cancel/close wakeups, owned input and cumulative metrics | native/Python nonblocking, cancel and flush tests | CPU writer complete for current queue/metric contract |
| `INT-STATE-001..003` | running/flushing/closed behavior | close/write-after-close checks | CPU writer subset complete |
| `TEST-CONT-001` | independent decode and metadata verification | FFmpeg + ffprobe | VP9 WebM encode case passing |
| `TEST-CODEC-001` | VP9 encode/decode round-trip with quality metrics | internal decode and Y-PSNR >= 28 dB | SSIM pending |
| `TEST-CODEC-002` | SVT-AV1 to libaom/FFmpeg round-trip | 30-frame PTS/order/count, Y-PSNR >= 28 dB, all 8-bit inputs | SSIM pending |
| `INT-INTEL-001/002` | oneVPL 2.x hardware session, internal NV12 surfaces, VP9/AV1 encode/decode and libwebm mux/demux | `mkvc_intel_vpl_probe`, `mkvc_intel_vpl_encode`, `mkvc_python_intel_roundtrip` in required-hardware mode | Linux VA-API public Writer/Capture passing for both codecs with ordered multi-SyncPoint encode/decode; zero-copy and Windows hardware run pending |
| `INT-PIPE-003`, `TEST-INTEL-002` | per-operation bitstream/surface ownership and oldest-first SyncPoint collection | VP9/AV1 encode and decode at AsyncDepth 1/2/4/8 plus injected collection failure | exact requested pending high-water mark, ordered output, best-effort SyncPoint cleanup, repeated idempotent close and post-failure session recreation passing |
| `INT-ERR-006`, `INT-PIPE-005`, `TEST-ERR-002` | test-build-only asynchronous backend failure injection | `mkvc_async_failure` with eight concurrent writers and queue capacity one | all blocked writers wake within timeout, terminal IO reaches close, queue stays bounded and a clean session can be recreated |
| `EXT-BACK-001` Intel | runtime capability exposes each Query-supported encode/decode direction | C ABI capability assertions with real hardware and oneVPL-disabled builds | no false direction advertisement; unavailable build omits Intel rows |
| `EXT-CS-001/002`, `INT-CS-001`, `TEST-CS-001` | .NET 8 P/Invoke types, typed exception, CPU/GPU frame SafeHandle ownership, IDisposable writer/capture, owned I420 result, leased `ReadSurface`, `WriteSurface`, descriptor/native-handle/wait access and strict GPU copy policy | `mkvc_dotnet_build`, `mkvc_dotnet_smoke`; GPU descriptor/native-handle/copy-policy ABI sizes asserted as 136/64/20 bytes; project-local official .NET SDK 8.0.415 performs warning-free SDK-style build and loads the Windows native DLL for a 10-frame CPU round-trip | Windows and Linux CPU managed smoke pass. GPU source API builds; Intel/NVIDIA managed hardware transcode smoke remains pending. Packed CPU formats, async Task API and NuGet publication are also pending |
| `INT-PERF-001`, `TEST-PERF-001/002` foundation | versioned public-API JSON benchmark parameterized by backend/codec/resolution/fps/queue/prefetch | `mkvc_python_benchmark_smoke` | end-to-end fps, submit latency distribution, first-frame latency, bytes, RSS and explicit copy path recorded; approved baselines/regression thresholds pending |
| `EXT-OBS-001`, `INT-OBS-001/003/004` aggregate subset | versioned C ABI/Python metrics snapshots for frame counts, queue wait, backend time, capacity/peak, GPU pending peak and actual copy path | CPU native round-trip, Python benchmark smoke and Intel public round-trip | bounded queue/high-water and four pending Intel operations observed; per-stage conversion/codec/mux and GPU-event timing pending |
| `INT-NV-001..003`, `TEST-NV-001`, `TEST-BACK-001`, `TEST-GPU-005` decode slice | runtime-only CUDA/NVCUVID loading, incremental libwebm demux, CPU I420 readback mode, and GPU mode returning leased mapped NVDEC CUDA pointers through the common C ABI/Python surface API | `mkvc_nvidia_probe`, `mkvc_nvidia_webm_decode` on Windows plus NVIDIA-free Linux strict build | RTX 2060 VP9 produces 30 ordered CUDA NV12 frames with pointer/context/pitch/plane offsets; holding the first lease does not drop frames; Capture close defers unmap/context destruction until final release. AV1 positive hardware, CUarray/event/stream export and trace proof remain pending |
| `INT-NV-001..006`, `TEST-NV-002..005`, `TEST-GPU-007` encode/transcode slice | runtime-loaded CUDA/NVENC AV1 P4 synchronous adapter; CPU inputs plus leased NVDEC CUDA-pointer NV12 registration/map/encode/unmap/unregister; same-context enforcement, nanosecond PTS propagation and libwebm mux through the common Writer | `mkvc_nvidia_webm_encode`, `mkvc_nvidia_gpu_transcode`, NVIDIA-enabled Windows build and strict Linux build | VP9 encode is always rejected; unsupported AV1 hardware and NVIDIA-free hosts skip/fail without output. Source implementation and lifetime cleanup are present; RTX 2060 passes NVDEC regressions but cannot encode AV1, so positive registered-resource encode, independent golden decode and DtoH/HtoD trace proof remain pending on an AV1-capable GPU |
| `EXT-GPU-008`, `INT-GPU-010/011`, `TEST-GPU-009/010` DLPack foundation | C ABI exports each linear CUDA NV12 plane as a standard uint8 `DLManagedTensor`; native deleter retains the GPU-frame lease independently of Python GC; CPython stable-ABI extension creates source capsules and consumes contiguous CUDA NV12 tensors; `GpuFrame.plane()` exposes the producer protocol and `GpuFrame.import_dlpack_nv12()` validates CUDA uint8 shape/stride before transferring the tensor deleter into the native frame lease; producer CUDA events are inserted into a nonzero consumer stream through `cuStreamWaitEvent` | native ABI tests validate Y/UV metadata and lease ordering; Python tests validate consumed/unconsumed capsule ownership, invalid-layout cleanup and exactly-once deleter; Windows NVIDIA hardware test validates context push/pop plus event-to-stream insertion | Linear CUDA DLPack export/import and native stream dependency complete. CuPy E2E qualification, subprocess shutdown stress, built-extension wheel inspection, CUarray and Intel USM remain pending |
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
