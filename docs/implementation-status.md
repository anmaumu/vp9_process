# Implementation status

この文書は仕様の正本を変更せず、実装・検証の到達状況を記録する。
同じ機能について複数の記録がある場合は、上にある新しい日付の記録を
現行状態として扱う。`PARTIAL`、`pending`、`unimplemented`などの表現は、
各見出しの日付時点の履歴であり、後続実装を否定しない。

## Documentation tooling

- `docgen check/generate/build` validates specification IDs and generates Markdown/HTML.
- `abi_guard check` fingerprints every C ABI function signature, enum value and
  public struct field against the reviewed v1 snapshot before refactors merge.
- `binding_guard check` requires Python ctypes and .NET P/Invoke to declare the
  complete function-symbol set extracted from the canonical public C header.
- `generate_bindings check` reproduces Python ctypes and .NET P/Invoke function
  signatures from that header and rejects stale checked-in generated code.
- C ABI and Python API references are extracted from source declarations.
- Doxygen generates C/C++ HTML and XML from public/internal source comments.
- Every exported `MKVC_API` symbol must have a Doxygen comment; missing comments fail docgen.
- GitHub Actions builds strict MkDocs HTML and stores `mkvcodec-documentation` for 30 days.
- GitHub Pages publication remains disabled until an explicit public-release decision.

## 2026-09-06: Python docstring quality gate

`docgen check` now rejects every source-defined public Python class, function,
or method that lacks a docstring, and validates NumPy-style section underlines.
Simple format-specific read/write methods document their copy and end-of-stream
semantics. Generated class references show the constructor signature directly
and no longer emit a separate empty `__init__` section.

## 2026-09-06: Encoder session responsibility split

CPU frame validation and asynchronous deep-copy ownership now live in
`src/encoder/cpu_frame_copy.*`, separate from queue scheduling and backend
lifecycle in `encoder_session.cpp`. The internal interface documents packed-row
ownership, borrowed validation, and view lifetime with Doxygen comments. Public
C ABI and language APIs are unchanged; Windows NVIDIA and Linux Intel complete
regression suites pass after the split.
Borrowed CPU submission ownership, terminal-state publication, nonblocking
query, and timed wait synchronization are isolated in
`src/encoder/cpu_submission.*`. The class remains in namespace `mkvc`, so C API
adapters retain their existing type and lifetime contract.
The type-erased encoder backend interface and its compile-time CPU/GPU adapter
now live in `src/encoder/encoder_backend.hpp`. This leaves session creation and
scheduling independent from the forwarding mechanics while preserving concrete
backend ownership and error propagation.
Queue state and the asynchronous worker loop now live in
`src/encoder/encoder_session_state.hpp` and `encoder_worker.*`. Ordered flush
barriers, capacity wakeups, injected failure propagation, borrowed completion,
metrics accumulation, and backend finalization remain under the same locking and
FIFO contract. `encoder_session.cpp` is reduced to session-facing operations and
backend selection.

## 2026-09-06: Python capture/writer split

`VideoWriter` and `VideoCapture` now live in `_writer.py` and `_capture.py`;
metrics, FPS normalization, and NumPy plane-pointer helpers live in
`_io_common.py`. `_io.py` remains a compatibility re-export, so public imports
through `mkvcodec` and the former internal module continue to resolve. Docgen
scans the physical implementation modules. Windows CPU/DLPack checks and the
complete Linux Intel VA/OpenCL suite pass after the split.

## 2026-09-06: Python GPU plane/interop split

The `GpuPlane` DLPack protocol adapter now lives in `_gpu_plane.py`, while
backend-neutral capability normalization lives in `_gpu_interop.py`. `GpuFrame`
continues to expose both through `_gpu.py`, including the source-tree extension
injection behavior used by hardware tests. Capsule ownership transfer, dependency
registration, device checks, and managed-tensor failure cleanup are unchanged.
Windows external-import checks and the complete Linux Intel suite pass.
External import implementations are separated into `_gpu_cuda.py` and
`_gpu_intel.py`, with process-unique generation and optional-extension lookup in
`_gpu_import_common.py`. Public `GpuFrame` classmethods retain their original
signatures and documentation as thin delegators. This keeps IDE/API reference
quality stable while isolating CUDA from D3D11/VA/USM validation and native calls.

## 2026-09-05: CMake test modules

Repository Python script tests and unittest suites now use two small CMake
helpers for interpreter selection, source-root path construction, and unittest
working-directory setup. The helpers and 13 repository-level checks now live in
`cmake/MkvcPythonChecks.cmake`, leaving the root build file focused on product
targets and platform test composition. Test names, arguments, properties,
fixtures, and platform conditions are unchanged. Windows and Linux
reconfiguration preserves their complete test inventories, and every migrated
check passes on both systems.
Python DLPack binding checks and codec integration tests are composed through
the same module as explicit functions. Registration order remains stable, and
the Intel GPU surface check receives the shared VP9 fixture path as an explicit
argument rather than relying on an implicit global.

NVIDIA-specific native test target definitions now live in
`cmake/MkvcNvidiaTests.cmake`. The module separates hardware-independent probe
and encoder checks from the tests that consume the shared CPU VP9 fixture, while
preserving the existing fixture dependency and hardware-skip contract. The root
build file retains only the platform condition and the two composition calls.
Intel oneVPL targets and Linux VA/OpenCL integration checks likewise live in
`cmake/MkvcIntelTests.cmake`. Repeated AV1 OpenCL modes share one registration
loop, while their individual test names, environment contracts, timeouts, and
fixtures remain stable.

## 2026-09-05: Python API responsibility split

The public Python import surface remains compatible while the former monolithic
`_api.py` is now a thin re-export facade. Data contracts, capability selection,
CPU leases/pools, GPU frame/DLPack interop, Intel USM pooling, and capture/writer
I/O live in separate dependency-directed modules. Source-tree GPU tests patch the
actual GPU interop module rather than relying on facade module globals.

Public Python classes and methods now use NumPy-style docstrings. The documentation
generator scans every physical API module and emits method docstrings as well as
signatures, so ownership, synchronization, and backpressure contracts remain
visible after the split. The C ABI fingerprint is unchanged. All 32 configured
Windows tests passed; the two hardware-dependent NVIDIA tests remained expected
skips on the current runner.

## 2026-09-05: C ABI language-binding synchronization

Python ctypes and .NET P/Invoke now declare all 58 public C ABI functions,
including previously internal-only retain, completion-query, processing,
nonblocking submission, result-string, and DLPack entry points. This declaration
completeness does not by itself make every low-level function a public idiomatic
language API; the high-level wrappers remain the supported user surface.

A NumPy-documented binding guard extracts the canonical symbol set from
`mkvc.h` and rejects missing or invented Python/.NET declarations. It runs in
CTest and documentation CI with negative tests for both failure directions.
Python compile/import coverage, the guard tests, and the project-local .NET 8
build and native smoke test pass. Struct layouts and parameter marshalling remain
checked by the existing ABI and managed smoke tests. Python and .NET structure
declarations are generated from the same header, with reviewed managed mappings
for public names and fixed-array marshalling.

## 2026-09-05: Generated Python/.NET native signatures

All 58 Python ctypes `argtypes`/`restype` declarations and all 58 .NET P/Invoke
methods are now generated deterministically from the normalized public C header.
A small reviewed mapping defines language representations for fixed-width
integers, structs, callbacks, and opaque handles; any new unmapped C type fails
closed instead of falling back to an unsafe default. The .NET mapping explicitly
preserves raw pointers for SafeHandle release/close and existing metrics calls,
while ordinary calls retain typed SafeHandle parameters. Hand-maintained files
retain library discovery, opaque handles and callbacks, ownership wrappers, and
error mapping.

Generated files are checked into source so wheel and NuGet builds do not require
the generator. Unit tests verify reproducibility, all public functions, unknown-
type rejection, SafeHandle release signatures, typed submission status, and
binding symbol completeness. CTest and documentation CI run the generator check
without rewriting the worktree; the .NET 8 build and native smoke test pass.

## 2026-09-06: Generated Python C ABI types

Python enum constants and all concrete ctypes structures are now generated from
the canonical `mkvc.h` ABI surface. The generator preserves fixed-width scalar
types, pointers, fixed arrays, and nested structures, and orders nested structure
definitions by dependency. Opaque handles, callback prototypes, dynamic-library
loading, ownership wrappers, and exception translation remain explicit Python
code because they express runtime policy rather than C data layout.

The generated module is checked into source so installed wheels do not need the
generator or C headers. Reproducibility checks reject stale output, and focused
tests require every public enum and structure field to be emitted and reject an
unknown field type instead of guessing its layout. The complete Windows suite
passes with the two hardware-dependent NVIDIA tests skipped as expected; the
same generated declarations are also validated by the complete Intel Linux suite.

## 2026-09-06: Generated .NET C ABI types

The public .NET ABI enums and all managed interop structures are now generated
from `mkvc.h`. A reviewed compatibility map preserves the existing idiomatic C#
names, public versus internal visibility, typed enum fields, nanosecond suffixes,
and fixed-array representation. CPU plane-pointer arrays remain four explicit
`nint` fields, while GPU descriptor arrays retain `ByValArray` marshalling; these
are deliberate managed interop policies rather than inferred naming conventions.

`MkvGpuInteropInfo`, SafeHandle classes, callback ownership, and high-level APIs
remain hand-maintained because they are managed behavior rather than C ABI data.
Generation fails closed if a mapped C enum gains an unknown member or if a
structure field cannot be represented. The .NET 8 build and native smoke test
verify the generated layouts and exercise CPU encode/decode, pools, submission,
GPU descriptors, and external-frame lifetime behavior.

## 2026-09-06: Isolated oneVPL external-surface import

Intel encoder D3D11/VA-API surface import is now isolated from encoder session,
queue, and bitstream logic in a Doxygen-documented interop module. The module
owns no resource: it validates the native handle, requests shared import from the
oneVPL memory interface, rejects copy-import fallback, and returns one referenced
surface for the encoder lease to release. This keeps OS-specific native-resource
rules at the GPU interop boundary without changing completion or backpressure.

The NVIDIA/CPU Windows suite and the complete Intel Linux suite pass after the
split. Intel tests cover real VA surface decode-to-encode, external OpenCL/USM
round trips, explicit copy auditing, asynchronous depth, and resource recycling.

## 2026-09-06: Isolated oneVPL bitstream conversion

Completed oneVPL bitstream interpretation is now separate from SyncPoint waiting,
input completion, and surface recycling. The Doxygen-documented converter strips
optional VP9 IVF file headers and mandatory per-frame headers before libwebm
muxing, passes AV1 payloads through, and normalizes timestamp and keyframe data.
Malformed or oversized VP9 frame declarations fail without appending a packet.

A hardware-independent Intel foundation test covers AV1 payload/offset handling,
VP9 IVF extraction, timestamps, keyframes, truncated headers, oversized frames,
and destination immutability on failure. It runs whenever oneVPL support is built;
the complete Intel Linux suite now contains 43 passing tests.

## 2026-09-06: Isolated oneVPL encoder queue

Intel encoder asynchronous submission is now owned by a dedicated,
Doxygen-documented queue. It owns each bitstream buffer until its SyncPoint is
collected, preserves FIFO packet order, tracks configured and observed pending
depth, and couples GPU input completion to the exact submitted surface lifetime.
Drain submission and close-time synchronization use the same queue boundary.

The encoder session remains responsible for device selection, video parameters,
surface acquisition/import, and imported-resource retention. The queue remains
responsible for `MFX_WRN_DEVICE_BUSY` retry, ordered SyncOperation, packet
conversion, completion success/failure, recycle polling, and injected device-loss
state. The complete Intel Linux suite passes with real GPU surface round trips;
the Windows CPU/NVIDIA configuration continues to build and pass independently.

## 2026-09-06: Isolated oneVPL decoder queue

Intel decoder asynchronous submission and pending surfaces are now owned by a
dedicated, Doxygen-documented queue. It retries device-busy submissions, preserves
FIFO SyncPoint collection, tracks pending depth, and releases every surface on
success, failure, injected device loss, or close. Drain behavior shares the same
queue logic instead of duplicating the submission loop in CPU and GPU paths.

CPU collection maps NV12 only after SyncOperation, converts it into owned I420,
then releases the oneVPL surface. GPU collection checks pool backpressure before
transferring the referenced surface and session lifetime into `GpuFrameCore`, so
no CPU mapping is introduced. Decoder session code now retains header parsing,
initialization, and compressed-packet feeding. Intel Linux real-surface tests and
the independent Windows CPU/NVIDIA build pass after the split.

## 2026-09-06: Shared optional-driver library loader

OS-specific `LoadLibrary`/`GetProcAddress` and `dlopen`/`dlsym` handling has been
removed from the NVDEC decoder state and placed in a small Doxygen-documented
RAII module. It owns only the module handle, is non-copyable, returns null for
missing libraries or symbols, and does not bundle or load a fallback GPU driver.
The NVDEC function table and CUDA context remain owned by the decoder.

A GPU-independent native test resolves a known operating-system symbol and
rejects an intentionally missing symbol on Windows and Linux. This makes dynamic
loader behavior testable on the Intel machine while the Windows NVIDIA tests
continue to validate actual CUDA/NVCUVID driver loading.

## 2026-09-06: Isolated NVDEC driver API table

The complete required CUDA/NVCUVID symbol set is now loaded and validated by a
Doxygen-documented `NvdecApi` table. It owns the CUDA and NVCUVID module handles,
so every resolved function pointer remains valid until parser, decoder, mapped
frames, and deferred CUDA context teardown have finished. Missing libraries or
any missing required symbol fail decoder creation without a partial API table.

The decoder state now contains one API object instead of duplicating library
handles and sixteen raw function-pointer members. CUDA context guards, callbacks,
readback, mapped-frame release, parser drain, and deferred close all use that
single table. The real Windows RTX 2060 NVDEC decode regression passes after the
change; the GPU-independent loader test continues to pass on both operating
systems.

## 2026-09-05: Scoped CUDA context activation

CUDA driver context push/pop is now represented by one Doxygen-documented,
non-owning RAII guard. CUDA event polling, consumer-stream dependency insertion,
NVDEC reads, mapped-frame release, deferred decoder teardown, and close cleanup
use the same pairing and context-identity validation. Early returns receive a
best-effort destructor pop, while normal paths explicitly validate the pop.

The guard does not destroy contexts, streams, events, arrays, or decoder-owned
resources, so the existing public ownership and deferred NVDEC frame-lifetime
contract is unchanged. The NVIDIA build, real CUDA event/stream test, and NVDEC
decode test pass; GPU transcode remains an expected skip when the configured
hardware codec path is unavailable.

## 2026-09-05: oneVPL runtime lifetime owner

Intel probe, decoder, and encoder now use one Doxygen-documented RAII owner for
the dispatcher loader, session, and initialized codec component. It always tears
down in component -> session -> loader order and also closes partially created
runtimes when any configuration or initialization step fails.

The decoder shares this owner with exported GPU surfaces, preserving the prior
rule that closing Capture cannot invalidate a live surface. The encoder retains
its deliberate imported-wrapper -> codec component -> session/loader -> original
external-owner release ordering. Linux oneVPL builds without warnings and all 40
tests pass, including VP9/AV1, VA surface synchronization, OpenCL processing,
external import, and Python surface lifetime coverage.

## 2026-09-05: C ABI translation-unit split

The former single C ABI implementation is separated into stable entry points,
encoder adapters, decoder/frame adapters, and CPU pool/submission adapters.
Opaque handle state and the thread-local error helper live in one internal header
with Doxygen ownership documentation. Public declarations, symbol names, calling
conventions, enum values, and struct layouts are unchanged.

The split builds with both MSVC/NVIDIA-enabled and GCC/Intel-enabled profiles.
All 32 configured Windows tests passed with two expected hardware skips; all 40
Linux tests passed after synchronizing the pre-existing ABI snapshot tooling.
`docgen-check` passes locally. Full HTML generation remains a CI/environment step
because the current Windows and Linux hosts do not have MkDocs/Doxygen installed.

## 2026-09-05: Shared incremental WebM packet reader

CPU VP9, CPU AV1, Intel oneVPL, and NVIDIA NVDEC now consume one backend-neutral
libwebm packet reader instead of maintaining four copies of track selection and
cluster/block/frame cursor logic. The Doxygen-documented reader validates the
container and requested codec track, enforces the existing 256 MiB packet limit,
preserves nanosecond PTS, and remains incremental rather than preloading media.

Targeted decode and round-trip tests pass on Windows/NVIDIA and Linux/Intel,
including VP9 metadata, Intel GPU surfaces, VA synchronization, OpenCL processing,
and AV1 output. Codec decode and GPU surface creation remain backend-specific.

## 2026-09-05: Shared WebM/Matroska muxer

CPU VP9, CPU AV1, Intel oneVPL, and NVIDIA NVENC now submit encoded packets to
one Doxygen-documented muxer. It owns libwebm writer/segment lifetime, VP9/AV1
track metadata, AV1 codec-private bytes, nanosecond packet timing, keyframe flags,
container finalization, and the WebM/MKV DocType patch. Codec packet acquisition
and backend-specific keyframe decisions remain in their encoder implementations.

All 32 Windows tests passed with two expected GPU hardware skips, and all 40
Linux Intel tests passed. The coverage includes CPU VP9 encode/decode/metadata,
NVENC AV1 creation, Intel VP9/AV1 round trips, GPU-surface encode, OpenCL external
processing, ABI guards, and artifact compliance checks.

## 2026-09-05: Type-erased encoder backend dispatch

`EncoderSession` now owns one `EncoderBackend` contract rather than four nullable
CPU VP9, CPU AV1, Intel, and NVIDIA pointers. A Doxygen-documented adapter exposes
write, flush, close, GPU-frame support, GPU submission, and hardware-pending
metrics uniformly. CPU adapters reject GPU frames explicitly; Intel and NVIDIA
adapters preserve their native zero-copy paths.

Targeted synchronous, asynchronous, cancellation/failure, C API, C++ RAII, CPU,
Intel GPU-surface, and NVIDIA encoder/event tests pass on both available hosts.

## 2026-09-05: Fixed-capacity external GPU/Intel USM pool

A versioned C ABI reservation pool now bounds caller-preallocated GPU resources
without linking the production library to CUDA, SYCL, D3D11 or VA allocators.
Reservations carry a slot index and monotonically changing per-slot generation;
zero-timeout acquisition returns `WOULD_BLOCK`, finite waits return `TIMEOUT`, and
the final reservation release wakes one waiter. Pool owner destruction does not
invalidate existing reservations. Capacity, current/peak occupancy, successful
acquisitions, rejected acquisitions and wait nanoseconds are observable.

Python `IntelUsmFramePool` binds fixed `(pointer, owner)` resources to this native
gate. An `IntelUsmPoolSlot` is acquired before producer submission and transfers
its reservation into the imported USM frame. The Arc return path keeps that USM
frame inside the VA owner through oneVPL encode, preventing early reuse after the
DLPack consumer finishes. C++ exposes move-only RAII pool/reservations; .NET uses
SafeHandle wrappers and permits a reservation to be the managed GPU-frame owner.

The Python slot wrapper now enforces an explicit one-way writable-to-transferred
or writable-to-released state transition. Double import and import after release
are rejected deterministically; closing a transferred wrapper cannot recycle its
reservation while the frame is live. Terminal wrappers also clear redundant
allocation-owner and dependency-registrar references, so an otherwise released
pool is not retained merely because an old slot object remains reachable.

Deterministic Windows/Linux tests cover capacity-one rejection, finite timeout,
blocked-waiter wakeup, generation 1->2, occupancy metrics, DLPack-delayed recycle
and pool-before-reservation destruction. Arc B580 passed eight AV1 frames with a
single 2 MiB allocation, peak occupancy one and seven explicit backpressure
events. Every frame/event/VA owner and the sole allocation was released; decoded
pixels and PTS passed. A four-slot 240-frame run also passed while reusing only
four allocations, with peak occupancy three and all 240 frame/event/dependency
owners released. The earlier non-pooled 30-minute run passed 97,792 frames in
3,056 batches, but does not replace a 30-minute qualification of this new pooled
path.

Traceability: EXT-GPU-002/009 -> INT-GPU-003/011 -> TEST-GPU-002/014.

## 2026-09-05: Intel USM consumer-queue dependency registration

The versioned C ABI now accepts a synchronous consumer dependency callback for
DLPack export. Python `GpuFrame.import_usm_nv12()` carries an optional
`dependency_registrar`; `__dlpack__(stream=...)` passes the borrowed Level Zero
producer event and actual consumer queue address to it. A successful registrar
allows export to return without a host wait. Missing adapters retain the previous
fail-safe host wait, and callback failures abort export. The production library
therefore remains independent of the SYCL C++ ABI while consumer runtimes can
provide their own event-import adapter.

Arc B580 validation passed eight AV1 frames with eight Level Zero event imports
and consumer queue barriers, pointer-identical kDLOneAPI sharing, dpnp processing,
linear VA shared import, pixel/PTS oracle checks and complete owner/allocation/event
release. Native unit coverage proves that a successful callback returns while the
producer completion is still pending. This removes the host wait at the
USM-to-DLPack edge; the test still completes consumer work before VA encode, so it
does not claim that the complete external-process-to-encoder edge is asynchronous.
An extended 240-frame run also passed with exactly 240 dependency registrations,
USM allocations, Level Zero events and VA owners released; wall time was 2.47 s and
peak process RSS was 646,864 KiB. This bounded run is not a substitute for the
separate 30-minute VRAM/RSS qualification.

The same test supports bounded same-process soak batches and persists a fixed-size
progress/failure report after every batch. The legacy non-pooled path subsequently
passed the full 30-minute gate: 3,056 batches and 97,792 frames in 1,800.4 seconds,
with bounded RSS, file descriptors, threads and `xe` VRAM evidence for PCI
`0000:83:00.0`. This result predates the fixed-capacity pool and therefore does not
qualify pooled allocation reuse or pool-exhaustion behavior.

Traceability: EXT-GPU-008 -> INT-GPU-010/011 -> TEST-GPU-004/009/010.

---

### Historical checkpoint boundary

以降は2026-09-04以前の時点別検証記録である。計測値、失敗経緯、当時の
リスク判断を失わないため保持しているが、現在の機能有無は上の最新記録と
末尾のtraceability tableを優先する。古い「未実装」「pending」は、その
checkpoint以後に完了している場合がある。

## 2026-09-04: Intel device-USM DLPack and Level Zero event adapter

The first versioned Intel USM slice is now public through the C ABI frame
descriptor, C++/.NET Level Zero event entry points and Python
`GpuFrame.import_usm_nv12()`. It accepts only positive/even
linear NV12 device-USM layouts with nonzero pointer, SYCL context and queue,
consistent caller-declared device/pitch metadata, and an owner lease. Producers
may either be explicitly synchronized or supply a borrowed native Level Zero
event. `mkvc_gpu_frame_import_level_zero_event()` dynamically loads only the
public `zeEventQueryStatus`, polls without device-wide synchronization, latches
terminal status and never resets or destroys the caller's event. Generic import
rejects a nonzero USM event so pending work cannot be silently marked complete.

`GpuFrame.plane()` exports the two planes as standard kDLOneAPI uint8 DLPack
tensors. The native tensor deleter retains the frame and allocation owner, and the
interop view now advertises `sycl_usm`/`dlpack` only for actual USM pointers. VA
surfaces and D3D11 textures are never re-labelled as linear tensors. oneVPL does
not consume the generic USM descriptor directly: encode still uses an explicitly
shared VA/D3D11 view, and decode tiled-image to linear-USM materialization remains
reported as `gpu_copy`, not zero-copy.

Arc B580 validation passed eight AV1 frames with public API enabled, identical
USM/DLPack pointers, a linear DMA-BUF/VA identity, external dpnp processing,
VA import to oneVPL encode, decoded-pixel/PTS oracle checks and all eight owner and
allocation and Level Zero event-owner releases. Native and Python negative/lifetime
tests pass on Windows and Linux. Consumer dependency registration and bounded
allocator pool/backpressure were completed by the 2026-09-05 slices above.
Non-linear and cross-context fault qualification, Windows Intel and pooled long
soak remain pending.

Traceability: EXT-GPU-004/008/009 -> INT-GPU-011 -> TEST-GPU-009/010/018.

## 2026-09-04: WebM/Matroska DocType enforcement

Container selection now follows the output/input extension at every CPU, Intel and
NVIDIA backend boundary. `.webm` is finalized and validated with EBML DocType
`webm`; `.mkv` is finalized as DocType `matroska`. Because libwebm derives WebM
from the VP9/AV1 track codec, Matroska output is installed by a bounded streaming
EBML-header rewrite after successful mux finalization. The replacement uses a
temporary sibling file and atomic rename, never buffers the media payload, and
keeps its 1 MiB transfer buffer off worker-thread stacks.

Decoders fail closed when the extension is unsupported, the EBML header is
malformed, or extension and DocType disagree. Python CPU coverage verifies both
formats, deliberate mismatch rejection and unsupported-extension rejection. The
Linux Intel suite also performs the AV1 hardware encode/decode roundtrip as actual
Matroska. Linux passed 37/37 tests; the Windows CPU/NVIDIA suite and independent
Python Matroska roundtrip pass after the worker-stack regression test.

Traceability: EXT-CONT-001/002 -> INT-CONT-001/002/004 -> TEST-CONT-001/002.

## 2026-09-04: Python auto backend and normalized GPU interop discovery

Implemented the previously specified Python `backend="auto"` path using the
native two-call runtime capability query. Selection is deterministic by codec and
direction; strict GPU residency excludes CPU before object creation and fails
instead of silently falling back. Capture/Writer expose the resolved `backend`.
The public `select_backend()` can require the decode/encode capability intersection
so one backend can be fixed across a complete externally processed pipeline.
When strict GPU residency is requested and queue/prefetch are omitted, the current
synchronous GPU requirement selects zero; an explicitly supplied nonzero value is
still rejected.

`GpuFrame.interop` now normalizes backend, memory/native-handle type, external
processor adapter family, DLPack availability and completion model.
`supports_interop()` permits adapter dispatch without an Intel/NVIDIA branch in
application control flow. It describes an already-created frame only and does not
claim that an external runtime, kernel, encoder or driver-internal zero-copy path
is available. Writer performs backend and dimension checks before native submit.

The Windows RTX 2060 exact auto/strict capture path selected NVIDIA and returned a
CUDA-pointer frame with CUDA/DLPack interop. Linux Intel auto/strict acquisition,
the OpenCL roundtrip and external import tests pass. Public Intel USM, asynchronous
GPU pipeline overlap and automatic propagation of one plan across separately
constructed Capture/Writer objects remain pending; `select_backend()` covers the
explicit shared-plan path.
The .NET binding exposes the same capability-intersection selector and normalized
`MkvGpuFrame.Interop` / `SupportsInterop` view; its build and native-load smoke pass.
The Linux suite passed 37/37. The Windows NVIDIA suite passed 24 tests with the
expected AV1 NVENC transcode skip on RTX 2060; docgen check and its three unit
tests passed.

Traceability: EXT-BACK-004 / EXT-GPU-001/005..010 -> TEST-BACK-001/002 / TEST-GPU-003/009.

## 2026-09-03: Default external-producer reuse and USM integration

Promoted the validated batch-scoped OpenCL reuse to the default in both external
OpenCL and experimental USM/DLPack roundtrips. No codec-core, public ABI or
packaged dependency change: the repeated compilation was in the external test
producer, not in the library. The low-level helper still requires an explicit
caller-owned session, avoiding a global cache with an unbounded display lifetime.

`MKVC_OPENCL_REUSE_PROGRAM=0` retains the explicit recreate baseline. Unprivileged
capture now defaults to reuse; `--recreate-program` selects the comparison and
`--reuse-program` remains accepted. Invalid/conflicting modes are rejected. The
privileged capture explicitly retains mode 0 for reproduction of its historical
baseline; no new privileged capture or permission change was performed.

Arc B580 captures with default arguments (`/tmp/mkvc-userspace-znqzmr2v`) and
`--recreate-program` (`/tmp/mkvc-userspace-5bwgp61t`) both passed their 32-frame
oracle. Application ISA allocations were 2 versus 64; matched ISA MAP requests
including the internal kernel were 3 versus 65; shared-image imports remained
64 in each. These remain partial allocation observations, not migration bytes.

The default USM path passed 240 frames with a single OpenCL build, identical
DLPack pointers, all 240 VA owners and all 240 USM allocations released, and the
CPU image/PTS oracle (`build/intel/usm_reuse_default_240.json`). Tiled decode to
linear USM still requires explicit GPU materialization; this is not a public
USM API or full decoder-to-tensor zero-copy.
The explicit recreate USM comparison also passed 8 frames
(`build/intel/usm_recreate_8.json`). Copies of these reports, the soak report and
both userspace capture directories are retained locally under
`build/qualification/opencl-default-reuse/` (untracked evidence).

The default 128x128 AV1 soak passed **60.440 seconds / 16,320 frames / 68 batches**,
peak retained owners 3. Post-close FDs stayed 6 and threads 26; RSS baseline/high
was 191,496,192 bytes, ending at 178,716,672. Arc resident VRAM baseline was
77,115,392 bytes and high/end 81,309,696 (+4 MiB). Engineering growth budgets
passed (`build/intel/opencl_default_reuse_soak_60.json`); this is not a 30-minute
qualification of the changed default or proof of no leaks/private driver copies.

All 37 configured Linux CTests passed, including the new explicit-recreate AV1
case, default/reuse cases, API copy audit and soak smoke. On Windows, the seven
session/config tests, five userspace parser tests, four kernel parser tests and
three oracle tests passed. Docgen check and its three unit tests also passed.

Traceability: INT-OBS-004 / INT-PERF-003 -> TEST-GPU-013/014/019/020.

## 2026-09-03: Controlled OpenCL program-reuse comparison

Added an opt-in, test-only `OpenClReuseSession`. Context, command queue, program
and kernels live for one batch; shared image objects remain per-frame and all
kernel arguments are reset before each enqueue. Each frame still performs
acquire -> two kernels -> release -> finish before returning the VA surface to
the encoder. A source anchor preserves display lifetime through cached-object
teardown. Overlapping use/close, changed display/device and reuse after failure
are rejected. Setup/processing failures make the session terminal and release
partially initialized cached resources. No OpenCL processing API was added to
the product. At this comparison stage the default still recreated resources;
the subsequent default change is recorded above.

Autonomous Arc 32-frame AV1 captures compare baseline
`/tmp/mkvc-userspace-pf6v5i5o` with reuse `/tmp/mkvc-userspace-1zwhqlby`:

| Observation | Recreate each frame | Reuse within batch |
|---|---:|---:|
| 64 KiB application `KERNEL_ISA` allocation records | 64 | 2 |
| Internal ISA allocation records | 1 | 1 |
| GPU-VA/handle matched ISA MAP requests (including internal) | 65 | 3 |
| 24 KiB shared-image import records | 64 | 64 |
| Image/PTS/count/owner oracle | passed | passed |

This controlled intervention strengthens the instruction-preparation explanation.
These are allocation/MAP-request counts, **not new kernel migration counts** or
completed copy bytes. We have not repeated the privileged kernel trace for this
comparison and do not infer full-path zero-copy from the reduced counts.

The reuse 128x128 AV1 same-process soak passed **60.632 seconds / 16,560 frames /
69 batches**, peak retained VA owners 3. Post-close RSS baseline/high-water was
191,660,032 bytes, ending at 178,556,928; FDs stayed 6 and threads 26. Post-close
Arc resident VRAM baseline 77,123,584 bytes, high-water/end 81,317,888 (+4 MiB).
Engineering growth budgets passed; this is not a 30-minute reuse qualification,
a throughput baseline or proof of zero leaks. Evidence: `opencl_reuse_soak_60.json`.

The independent exported-API audit of the reuse AV1 path also passed: 64 kernel
calls, 32 acquire/release/derive calls, no watched host transfer/map calls and no
binding conflicts. Unbound/private driver APIs remain outside coverage. Evidence:
`opencl_reuse_av1_copy_audit.json`. Arc VP9 encoding of the small default fixture
was unavailable; this explicit Arc qualification uses AV1 and 128x128 input.

Added four GPU-free cache-lifetime/failure tests, a reuse-mode analyzer test and
`mkvc_intel_opencl_reuse_av1_roundtrip`. All seven selected Linux CTests passed;
the GPU-free cache and analyzer tests also passed on Windows. Run comparisons
with `tools/capture_intel_userspace_trace.py` and its `--reuse-program` option.

Traceability: INT-OBS-004 / INT-PERF-003 -> TEST-GPU-013/014/019/020.

## 2026-09-03: Autonomous userspace attribution of OpenCL instruction buffers

Status: `PARTIAL` (strong evidence for instruction-code preparation, not a
complete driver-internal image-copy proof).

The corrected privileged run `/tmp/mkvc-kernel-phases-yld69ezz` completed its
32-frame oracle and both perf commands with exit 0. All 65 source-present
64 KiB GTT -> VRAM move records fell in the external OpenCL interval: three on
frame 0 and two on each subsequent frame. The other 809 move records had no
source (clear path). The trace also recorded 465 CPU faults and 1,949 GPU jobs.
This localized the observation but did not identify kernel BO contents.

Without any further user action or sudo, enabled diagnostic NEO logging only
in child test processes and added finer OpenCL phase delimiters. The installed
runtime is `intel-opencl-icd 26.09.37435.12-1~24.04~ppa1`; reference source tag
`26.09.37435.12` is commit `58c78d46922d972cd386c45b910cb44b45be0b5b`.
No driver replacement, host security change or new shipped dependency was made.

Two complete 32-frame captures (`/tmp/mkvc-userspace-fnoh4d6r` and
`/tmp/mkvc-userspace-kwh7nusy`) show:

| Userspace observation | Count | Interval |
|---|---:|---|
| 64 KiB `KERNEL_ISA` local-memory allocations | 64 | program build (two per frame) |
| 64 KiB `KERNEL_ISA_INTERNAL` allocation | 1 | initial discovery |
| GPU-VA + handle matched ISA MAP requests | 32 + 1 internal | enqueue invert |
| GPU-VA + handle matched ISA MAP requests | 32 | enqueue neutral |
| 24 KiB PRIME shared-image import records | 64 | image sharing (two BOs per frame) |

Thus the 65-count pattern is strongly consistent with instruction-code
preparation/residency, not evidence of pixel downloads. This is cross-run
corroboration, not a simultaneous kernel-pointer/BO-handle identity proof.
The source explicitly creates ISA allocations from kernel heaps and transfers
kernel bytes to them: [kernel_info.cpp](https://github.com/intel/compute-runtime/blob/26.09.37435.12/shared/source/program/kernel_info.cpp).
At this capture stage the test recreated context/program/kernels each frame;
the subsequent test-only reuse implementation and default change are recorded above.

`tools/capture_intel_userspace_trace.py` now runs the bounded capture autonomously
as an ordinary user. Missing runtime logging, incomplete oracle, mismatched
journals and malformed allocation records cannot produce accepted observations.
The parser matches GPU VA and handle, invalidates labels on GEM close/recreate,
and keeps `complete_copy_proof=false`. Numeric GEM handles, GPU addresses and
kernel BO addresses remain different namespaces. C stdio is drained at phase
boundaries; concurrent driver logging/timing perturbation remains a limitation.

Four GPU-free parser tests pass on Windows/Linux, and all six affected Linux
CTests (fixtures, VP9/AV1, soak, exported-API audit) pass. A separate unprivileged
strace ioctl/backtrace probe also completed, but its undecoded ioctl payloads
and symbol offsets were insufficient for buffer attribution. No further manual
kernel capture is requested for this investigation.

Traceability: INT-OBS-004 -> TEST-GPU-013/014; public USM API and complete driver
copy qualification remain open.

## 2026-09-03: Terminal-dependent CPU oracle timeout fixed

The user's `/tmp/mkvc-kernel-phases-akksa3y0` capture recorded 3,291 kernel
samples, but the workload exited 124 after its 120-second timeout. Its journal
reached frame 31, encoder flush/close, then `cpu_output_oracle`; the workload
report remained `not_completed`. This capture is not a passing validation and
has not been retroactively marked successful.

Reproduced without sudo using SSH with a controlling terminal and `timeout`:
the Python parent and FFmpeg child both entered stopped (`T`) state in a
background process group at the CPU oracle. FFmpeg's inherited terminal stdin
caused job-control stopping, not a demonstrated GPU encode hang. The normal
non-TTY SSH/CTest runs had not exposed this harness defect.

File-based FFmpeg/ffprobe oracles now use stdin=DEVNULL; FFmpeg also receives
`-nostdin`. The capture runner disconnects workload stdin after interactive
sudo authentication. A shared test helper covers both OpenCL and USM tests.
The same SSH-TTY/background-group 32-frame roundtrip now exits zero and records
`run_complete`; three GPU-free oracle tests and six affected Linux CTests pass.
The privileged combined capture must be rerun; existing failed evidence remains
untouched. Reference: [FFmpeg stdin interaction](https://ffmpeg.org/ffmpeg.html#Advanced-options).

Traceability: TEST-GPU-013/014, TEST-ERR-001 (test harness, not product ABI).

## 2026-09-03: User-authorized kernel trace and phase-correlation tooling

Status: `PARTIAL` (kernel events obtained; image-buffer identity and complete
driver copy attribution remain unresolved).

The user ran a bounded privileged perf capture with an unprivileged 32-frame
Arc AV1 workload. `/tmp/mkvc-kernel-trace.OUr7vA/events.txt` has 3,295 events:
1,956 `xe_sched_job_exec`, 874 `xe_bo_move`, and 465 `xe_bo_cpu_fault` events.
Of the moves, 809 have `move_lacks_source=yes` (source-absent/clear path), while
65 have `move_lacks_source=no`, GTT -> VRAM0, each with 65,536-byte BO size.
Those 65 records contain eight distinct kernel address values; address reuse
prevents interpreting that as eight unique objects. BO sizes are not completed
transfer-byte measurements. CPU faults do not establish pixel downloads.
Interpretation reference: [Linux v7.0 Xe BO implementation](https://github.com/torvalds/linux/blob/v7.0/drivers/gpu/drm/xe/xe_bo.c).
The host runs Ubuntu kernel 7.0.0-28-generic; distro-specific source differences
and independent kernel-worker activity remain outside this qualification.

Added an opt-in CLOCK_MONOTONIC phase journal and metadata-only VA export
observations around decode, external processing, import, encode, teardown and
the CPU oracle. Export overhead is explicitly marked; instrumentation can
perturb allocation behavior. A separate 32-frame hardware smoke passed with
one decoded and 32 processed exports, each reporting 24,576-byte DMA-BUF
objects and modifier `0x0100000000000009`. This is a different run, and neither
size comparison nor a DMA-BUF inode identifies the kernel BO in the old trace.

`tools/capture_intel_kernel_trace.py` uses sudo only for fixed perf commands,
runs the workload as the invoking user, selects `--clockid mono`, preserves
capture diagnostics/workload status and produces a fresh private result directory.
No system permission settings are changed. `tools/analyze_intel_kernel_trace.py`
rejects malformed/lost-text records, wrong devices, job errors, failed journals
and clock/thread non-overlap. It reports main-thread phase coincidence, leaves
other threads unattributed and never returns a complete-copy-proof status.
Legacy-log analysis and four GPU-free unit tests passed on Windows/Linux;
six affected Linux CTests (including fixtures, AV1, VP9, soak and exported-API
audit) passed. Combined privileged phase capture still requires the user's
interactive sudo execution; sudo authorization does not carry across SSH sessions.

Traceability: INT-OBS-004 / INT-PERF-003 -> TEST-GPU-013/014.

## 2026-09-03: Arc AV1, DRM memory telemetry and experimental USM return path

Status: `PARTIAL` (real USM/DLPack experiment succeeds; public USM adapter and
complete driver-internal copy qualification are not complete)

Added test-only Linux render-node selection to distinguish integrated Intel
Graphics (renderD128 / 0000:00:02.0) from Arc B580 (renderD129 / 0000:83:00.0).
The OpenCL producer independently reports its PCI identity; VRAM-required tests
require telemetry from that exact device, not another GPU enumerated in-process.
No production device-selection API or global machine settings were changed.

The Arc external AV1 test exposed a real timestamp bug: imported-surface PTS was
restored immediately after asynchronous submission. The AV1 runtime read the
restored zero later, producing valid images with every PTS equal to zero. Private
imported wrappers now keep submitted metadata throughout their runtime lifetime.
The direct decoder-owned path is unchanged and has separate qualification.
CTest now generates a 128x128 VP9 fixture and checks external OpenCL -> AV1
with CPU FFmpeg decoding, exact frame count, increasing PTS and Y-PSNR >25 dB.

DRM fdinfo sampling records active and post-close client memory per PCI device,
deduplicates duplicate file descriptors by client identity, and keeps a bounded
baseline/high-water/last report. Resident/total/system/VRAM categories stay
separate. Shared objects can overlap across clients, so these sums are not a
claim about unique physical bytes or system-wide VRAM. Growth budget is +256 MiB
per observed field; unavailable requested VRAM evidence fails rather than zeroing.
The Arc B580 AV1 soak passed in **1801.335 seconds**, completing **131,040 frames
in 546 batches** (240 frames/batch, 128x128). All owner releases and CPU image /
PTS oracles passed; peak retained external owners was 3. Post-close RSS was
190,652,416 bytes at baseline/high-water and 178,913,280 bytes at the end; FDs
stayed at 6 and threads at 26. Arc resident VRAM was 77,123,584 bytes post-close
at baseline and 81,317,888 bytes at high-water/end (+4 MiB); active resident VRAM
peaked at 99,098,624 bytes. The 4,914 samples passed the engineering growth
budgets, not a proof of zero leakage. This small-frame lifecycle qualification
does not qualify 1080p/4K, other devices, slow consumers or driver-internal copies.
Other diagnostic GPU workloads ran concurrently, so this is not a throughput
benchmark. Evidence: `arc_soak_30m.json`, child PID 246231, output codec AV1
(selected in the invocation; the running report version omits the codec field).

Intel USM feasibility progressed to a real **240-frame** experimental roundtrip:
VP9 decode tiled surface -> external OpenCL write to a separate linear device-USM
allocation -> DLPack pointer-identical sharing -> dpnp GPU operation -> same
allocation imported as VA NV12 -> oneVPL AV1 encode -> CPU golden decode.
All 240 VA owners and all 240 explicit USM allocations are released. VA re-export
preserves the DMA-BUF identity, linear modifier and plane layout. PTS is checked
against index/30 within Matroska's millisecond resolution. This is a test harness,
**not a shipped C/Python USM API** and not full-path zero-copy: the first image to
linear-allocation edge is explicit GPU materialization.

Safety findings: actual decoder/default NV12 exports have modifier
`0x0100000000000009`, including a default-allocation request listing only linear.
They cannot be presented as ordinary strided tensors. Dedicated Level Zero
device allocations explicitly requesting DMA-BUF export produce modifier 0 VA
imports. The experiment uses 2 MiB allocations to establish physical extent,
not an optimized production pool. Arbitrary pooled USM is rejected when the
logical extent differs from the exported buffer extent. Level Zero owns the
exported fd: the adapter duplicates it and closes only the duplicate. VA's
freshly exported fds have different ownership and are closed by the caller.
[Level Zero export ownership](https://oneapi-src.github.io/level-zero-spec/level-zero/latest/core/api/apis/mem.html#ze-external-memory-export-fd-t)

The optional helper is built by `tools/build_intel_usm_probe.py` against a local,
matching SYCL runtime; nothing new is linked into mkvcodec or bundled in packages.
The isolated test environment uses dpctl 0.22.1, dpnp 0.20.0 and SYCL 2026.1.1.
OpenCL-Headers revision: `c4c8fd6f9556c92b212308880854e6294d61b314`.
Evidence: `arc_usm_roundtrip_240.json`. Linux Intel CTest passed 31/31;
Windows CTest passed 24 with one expected RTX 2060 AV1-encode skip (25 total).
See the test specification for reproducible commands and pending acceptance work.

Copy instrumentation now forwards up to 16 distinct implementations per symbol
and tests a second dlmopen namespace. Exhaustion still fails closed. Reports
include loaded-object/runtime coverage. This does not fill private driver gaps:
kernel perf/tracefs access is denied (perf_event_paranoid=4, tracefs root-only,
no passwordless sudo). An additional libva trace had missing/sparse regions and
is not accepted as complete evidence. No host security/profiling settings changed.

Traceability: EXT-GPU-004/008/010 -> INT-GPU-011/017 / INT-PERF-003 ->
AC-GPU-001 / TEST-GPU-005/008/009/013/014/019/020; RISK-GPU-006/008/010/011.

## 2026-09-03: Independent copy observations and same-process soak

Status: `PARTIAL` (exported-API instrumentation and short-soak gates implemented;
complete driver traces, 30-minute/VRAM qualification and Intel USM remain open)

Added a test-only Linux glibc x86-64 LD_AUDIT module, not included in product
libraries/wheel/NuGet. It observes libva/OpenCL exported API calls independently
of the library's copy metrics, including RTLD_LOCAL/dlsym and OpenCL extension
function pointers. A GPU-free synthetic library exercises all **14 wrappers**
and checks that deliberately invoked host transfers/maps fail the positive gate.
Report validation rejects missing observations, invalid entries, ambiguous
bindings and wrong child PID; missing reports and child timeouts cannot pass.
Old result files are invalidated before each run.

On linux-machine's real 32-frame external OpenCL roundtrip, the audit observes
**64 kernels, 32 acquire calls, 32 release calls and 32 vaDeriveImage calls**,
with **0 watched host-transfer/map calls and 0 binding conflicts**. vaPutImage
is bound but unused; other watched host-transfer/map symbols are unbound in
this run. vaMapBuffer/vaMapBuffer2 are also unbound, not evidence that the
encoder never maps bitstreams. vaDeriveImage is metadata, not a pixel download
by itself. These are attempted-call counts, not transferred-byte counts.
Private/internal/vtable driver calls and GPU copies remain unqualified; this
partial trace does **not** prove the full driver path is zero-copy or support USM.

The external OpenCL test now supports bounded repeated batches in one process,
including Capture/Writer lifecycle, import-owner GC and a CPU PTS/count/PSNR
oracle. Each batch releases every external owner. Fixed-size JSON records the
post-close RSS/FD/thread baseline, high-water and last sample; incomplete/failed
runs do not receive a passed status. Engineering regression budgets after the
first warm-up batch are +256 MiB RSS, +2 FDs and +4 threads, not performance SLAs.

A **60.69-second** run completes **82 batches × 240 = 19,680 frames**. Owner
peak is **5**, with all owners released after every batch. FDs stay **6**, threads
stay **25**, and RSS goes from **184,512,512 to 190,386,176 bytes** (about +5.6 MiB).
This qualifies a short lifecycle repetition, not a 30-minute soak, VRAM plateau,
pool exhaustion, slow-consumer behavior or general long-term leak freedom.
Reports are `build/intel/intel_opencl_copy_audit.json` and
`build/intel/intel_opencl_soak_60s.json` on the test host; invocation and scope
are recorded in the test specification. The CI smoke uses two seconds/minimum
two batches; longer execution is opt-in and has not been scheduled.

Validation: Linux **27/27 CTests pass** with required Intel external import.
Windows **22 pass, 1 expected RTX 2060 AV1 NVENC skip**, including D3D11 fence,
CUDA import/DLPack and .NET build/smoke. Specification/docgen checks pass.

Intel USM remains unimplemented: the current native/OpenCL image path is kept
separate from linear USM/DLPack. A safe no-copy allocation identity/layout and
encoder return path must be established before advertising that capability;
an explicit GPU-copy adapter, if needed, must not be called zero-copy.

Traceability: EXT-GPU-010 -> INT-OBS-004 / INT-PERF-003 ->
AC-GPU-001 / TEST-GPU-013/014; RISK-GPU-010/011.

## 2026-09-02: D3D11 fences and real external OpenCL roundtrip

Status: `PARTIAL` (D3D11 synchronization and Linux external processing qualified;
Windows Intel encode, independent copy traces and Intel USM remain unfinished)

Added native D3D11 fence import through the C ABI, C++ RAII, Python and .NET.
The adapter validates matching NV12 texture dimensions, a single GPU-only
subresource and canonical same-device COM identity. It retains texture/fence
references, polls GetCompletedValue, latches terminal results and reports device
removal as failure. The producer must Signal and dispatch its commands; the
library does not Flush, Map, copy or perform a device-wide wait. This implements
native host-side fence polling, not a GPU-queue dependency insertion.

The Windows D3D11 hardware test passes pending/timeout/recovery, different-device
rejection, invalid descriptors, same texture/fence identity, GPU CopyResource
pixel verification and caller-first COM/owner release. The default 128 iterations
and an additional **4096-iteration** run pass. The readback oracle is test-only.
This validates D3D11 on the available Windows GPU, not Intel oneVPL encode.

Linux now has a real external-producer test: oneVPL VP9 decode -> OpenCL shared
VA images -> luma inversion/neutral chroma in another VA surface -> explicit
OpenCL release/finish -> Python synchronized VA import -> oneVPL VP9 encode.
CPU reference decoding checks count, increasing PTS and luma PSNR > 25 dB.
OpenCL code and kernels exist only under tests, not in the shipped library.

This test exposed a real ownership defect: an output SyncPoint did not imply
that the runtime had released all references to the imported input. Destroying
successive caller-allocated VA surfaces caused crashes/hangs. The encoder now
holds the imported wrapper and an actual owner lease until GetRefCounter=1 and
Data.Locked=0, then releases the wrapper before its original resource. During
close, its wrapper references are released before component shutdown, while
original owners survive through session/loader teardown. Retention is capped at
64 wrappers; exceeding it returns WOULD_BLOCK and requires flush/retry. Unknown
refcount state is retained rather than guessed safe. The existing first-frame
device anchor remains. Deterministic tests cover refcount/Locked/error decisions.
[Intel surface lifetime contract](https://github.com/intel/libvpl/blob/v2.14.0/doc/surface_sharing_apis_overview.md#reference-counting-and-release-of-imported-surfaces)

Validation: Linux **23/23 CTests pass**, with required Intel external import.
The default external OpenCL test is 32 frames; additional 240-frame and
**10000-frame** runs pass. In the 10000-frame run the owner peak is **5**, all
10000 owners are released after close, and runtime is about 22 seconds. This is
a repetition test, not 30-minute soak, a VRAM/RSS leak proof or an approved
performance baseline. Windows regression passes **21 tests with 1 expected
RTX 2060 AV1 NVENC skip**. The Intel encoder TU also compiles with MSVC /W4 /WX
using the oneVPL headers; this does not substitute for Windows Intel hardware.

Copy qualification is partial: producer code does not map/read/write host pixel
buffers, VA import requests shared-only, and the library encoder metric reports
zero_copy. The external kernel intentionally writes a separate GPU allocation;
that is not an identity-preserving in-place operation. Driver-internal copying
still requires independent trace evidence. Missing-symbol/fence/refcount-limit
fault injection, real device removal and long-run shutdown stress remain open.

Intel USM feasibility: linux-machine advertises both OpenCL VA media sharing and
unified shared memory, but the former exports images, not linear USM pointers.
The presence of both extensions does not establish a no-copy image/USM bridge
or a USM-to-oneVPL encoder path. USM/DLPack support is **not advertised or
implemented**; its allocation identity, layout, context and synchronization
contract remains the next implementation prerequisite. Existing native image
sharing is available without misrepresenting it as a tensor.

Traceability: EXT-GPU-003/004/006/009 -> INT-GPU-004/006/007/011/017 ->
AC-GPU-001 / TEST-GPU-003/004/006/014/019/020; RISK-GPU-008/011/016.

## 2026-09-02: Linux native VA producer completion and Python import

Status: `PARTIAL` (Linux VA slice implemented; cross-platform GPU interop remains partial)

Added `mkvc_gpu_frame_import_va_surface`, C++ `GpuFrame::import_va_surface`,
Python `GpuFrame.import_va_surface`, and .NET `MkvGpuFrame.ImportVaSurface`.
Linux Intel builds load the host's `libva.so.2` and poll the individual surface
through `vaSyncSurface2(timeout_ns=0)`. Pending work remains pending; terminal
success/failure is latched so later queries do not accidentally wait for encoder
consumer work. Missing entry points and driver UNIMPLEMENTED fail closed, with
no blocking `vaSyncSurface` or CPU-copy fallback. libva is recorded as an external
MIT runtime dependency in the manifest/SBOM, not bundled into wheel/NuGet.

The owner retains both display and surface; failed imports do not acquire owner
ownership or invoke its release callback. VA ID 0 is valid and UINT32_MAX is not.
All producer work must be submitted before import. Native VA polling covers
VA-submitted work only, not independent OpenCL/SYCL writes. Those require a
producer query or explicit external synchronization (`producer_synchronized=True`
in Python). The first external frame still anchors the encoder display until
flush/close, reserving one pool slot.

Validation on `linux-machine`: **22/22 CTests passed**, no skip, with
`MKVC_REQUIRE_INTEL_EXTERNAL_IMPORT=1`. New native synchronization E2E checks
eight VP9 frames, flush/rebind, mismatched display rejection, ownership, and
CPU-decoded count/PTS/luma PSNR. New Python E2E checks four frames, capture-first
close, owner retention through writer close, final GC, strict copy-path metrics,
and decoded frame shape/pixel variation. Deterministic tests cover pending,
timeout, success/failure latching, driver UNIMPLEMENTED, invalid arguments,
library keepalive and failed-import owner cleanup. As before, the optional CPU
AV1 reference decoder is disabled in this Linux build; Intel AV1 tests still run.

Windows NVIDIA regression: **20 passed, 1 expected skip** (RTX 2060 lacks AV1
NVENC). This includes .NET build/smoke and native/Python negative VA import tests.
Docgen validation passes. Windows does not claim positive VA or Intel hardware
qualification. Hardware VA tests use completed decoder surfaces: actual pending
VA workloads/races, missing-symbol loader fault injection, external processing
kernels and independent driver/API traces remain to qualify. D3D11 native fences,
Intel USM/DLPack and positive NVIDIA AV1 encode remain separate unfinished work.

Traceability: EXT-GPU-003/004/009 -> INT-GPU-004/005/007/017 ->
AC-GPU-001 / TEST-GPU-004/019/020; risks RISK-GPU-004/016/017.

## 2026-09-02: Intel external shared-surface import qualification

Status: `PARTIAL`

Correction to the September 1 result: `ImportFrameSurface` already exists in
memory interface 1.0, as confirmed by the Intel v2.10.2 header. Requiring 1.1 was
an implementation error, not a limitation of `linux-machine`. The contract test
now accepts 1.0 and additive minor revisions and rejects unknown major versions
and null import functions. [Intel v2.10.2 header](https://github.com/intel/libvpl/blob/v2.10.2/api/vpl/mfxmemory.h)

The writer binds its first external input to a new video-memory encoder using
the resource's VA display (or D3D11 device), requests only shared import, and
rejects a different device/display before vendor submission. CPU/direct input
must be flushed before changing to external input. The first external frame is
retained as a device lifetime anchor until flush/close; applications must allow
one retained pool slot. At this checkpoint the existing producer query callback
was waited before import; the subsequent native VA completion slice is recorded
above. Windows native producer fences remain pending.

On `linux-machine`, runtime 25.4/API 2.15/interface 1.0 now passes VP9 external VA
import -> encode -> CPU decode: eight frames, flush/rebind, delayed producer query,
different-display rejection, caller-first release, exactly-once owner release,
frame count, increasing PTS and luma PSNR > 25 dB against the fixture gradient.
`MKVC_REQUIRE_INTEL_EXTERNAL_IMPORT=1` is enabled for this positive qualification.
Missing runtime support still skips ordinary runs and fails required runs.
Windows D3D11 and external AV1 hardware qualification, external GPU processing
kernels, and independent driver/API trace proof remain unqualified.

Validation: Linux Intel build passes all 20 CTests (the optional CPU AV1 reference
decoder is explicitly disabled by build configuration; Intel AV1 tests still run).
FFmpeg independently decodes the external-import output and ffprobe confirms
eight VP9 frames. Windows NVIDIA/.NET regression passes 20 tests with one expected
AV1 NVENC transcode skip on RTX 2060. The Windows D3D11 encoder translation unit
also compiles under MSVC `/W4 /WX` with vendor-header warnings excluded. Docgen
validation passes; this does not qualify Windows Intel hardware execution.

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
| `EXT-CONT-001..003` / `AC-CONT-001` | libwebm mux/finalize/demux plus extension-selected EBML DocType validation and bounded atomic Matroska header installation | `mkvc_python_roundtrip`, `mkvc_python_intel_roundtrip`, `mkvc_cpu_vp9_external_decode`, `mkvc_cpu_vp9_metadata` | WebM and Matroska distinction complete for CPU/Intel/NVIDIA backend entry points; external oracle currently covers VP9 WebM |
| `EXT-ENC-001` | create/write/flush/idempotent close/destroy | `mkvc_cpu_vp9_encode` | synchronous and bounded asynchronous CPU paths complete |
| `EXT-ENC-002` | BGR/RGB/BGRA/I420/NV12 CPU input | VP9/AV1 Python round-trips | complete for both CPU writers |
| `EXT-ENC-005` | asynchronous input deep-copied before return | mutable reused inputs in native/Python round-trip | complete for supported CPU formats |
| `EXT-DEC-006`, `EXT-ENC-011`, `EXT-FRAME-006..007` | retained native I420 descriptor, Python read-only borrowed NumPy views, synchronous borrowed encode | native VP9 and `mkvc_python_roundtrip` lifetime/round-trip coverage | initial C ABI/Python slice complete; borrowed encode currently requires `queue_size=0` |
| `EXT-ENC-012`, `EXT-FRAME-009` | C ABI/Python async borrowed submission with query/wait/release and owner retention | native failure injection and Python GC/round-trip coverage | initial implementation complete; input mutation is prohibited until terminal completion |
| `EXT-FRAME-011` | `WriteBorrowedI420` short-duration managed pin plus `MkvCpuFramePool` writable unmanaged spans and `MkvSubmission` completion lease | `.NET` native-load, borrowed and pooled async round-trip smoke | synchronous short-pin and asynchronous unmanaged pool complete; optional OS page-lock metrics remain pending |
| `EXT-ENC-013`, `EXT-FRAME-010..012` native-pool subset | fixed-capacity C ABI pool, generation-checked slot lease, nonblocking/timed backpressure, Python NumPy/.NET Span views, async encoder ownership transfer | `mkvc_cpu_frame_pool`, Python/.NET capacity/generation/view-lifetime/round-trip coverage | native allocation slice complete; OS page-lock, strict fallback and detailed copy trace remain pending |
| C++ RAII facade | header-only move-only `Encoder`, `Decoder`, CPU/GPU `Frame`, `CpuFramePool`, `CpuBuffer`, `Submission` over the stable C ABI; typed `ResultError` retains the native result | `mkvc_cpp_raii` move/lifetime/generation/async encode/decode round-trip | common CPU path complete and GPU source/sink facade compile-qualified; Intel/NVIDIA C++ hardware round-trip remains pending |
| `EXT-GPU-004..005`, `INT-GPU-005/008/010` external-frame subset | common external leases and CUDA event import; CUDA pointer/array bind to NVENC; Intel D3D11/VA bind to same-device video-memory encoder and oneVPL interface-1.x shared import | callback lifecycle tests, CUDA context/event/array tests, Intel version gate and Linux VP9 external VA eight-frame encode/flush/CPU-decode test | Linux Intel VA positive and CUDA interop qualified; Windows Intel, external Intel AV1, native fence/VA sync and NVIDIA AV1 positive encode remain |
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
| `INT-CONT-001..004` | libwebm mux/demux, VP9/AV1 track IDs, PTS/duration/keyframe and bounded extension-selected DocType enforcement | FFmpeg/ffprobe plus Python CPU/Intel roundtrips and negative mismatch cases | WebM/Matroska container distinction complete; broader malformed-input fuzzing remains pending |
| `INT-CPU-004` / `INT-PIPE-001/005/006` | bounded worker queue, reusable frame buffers, cancel/close wakeups, owned input and cumulative metrics | native/Python nonblocking, cancel and flush tests | CPU writer complete for current queue/metric contract |
| `INT-STATE-001..003` | running/flushing/closed behavior | close/write-after-close checks | CPU writer subset complete |
| `TEST-CONT-001/002` | independent decode/metadata plus DocType, mismatch and unsupported-extension verification | FFmpeg + ffprobe and Python CPU/Intel integration | VP9 WebM oracle and CPU/Intel WebM/Matroska roundtrips passing |
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
| `INT-NV-001..006`, `TEST-NV-002..005`, `TEST-GPU-007` encode/transcode slice | runtime-loaded CUDA/NVENC AV1 P4 synchronous adapter; CPU inputs plus leased CUDA pointer/array NV12 registration/map/encode/unmap/unregister; same-context enforcement, nanosecond PTS propagation and libwebm mux through the common Writer | `mkvc_nvidia_webm_encode`, `mkvc_nvidia_gpu_transcode`, NVIDIA-enabled Windows build and strict Linux build | VP9 encode is always rejected; unsupported AV1 hardware and NVIDIA-free hosts skip/fail without output. Source implementation and lifetime cleanup are present; RTX 2060 passes NVDEC, CUDA event/stream and real CUarray import regressions but cannot encode AV1, so positive pointer/array registered-resource encode, independent golden decode and DtoH/HtoD trace proof remain pending on an AV1-capable GPU |
| `EXT-GPU-008`, `INT-GPU-010/011`, `TEST-GPU-009/010/014` DLPack foundation | C ABI exports linear CUDA and Intel device-USM NV12 planes as standard uint8 `DLManagedTensor`; Python imports synchronized USM or a borrowed Level Zero producer event with owner/context/queue/layout validation; native deleter retains every frame/event owner; optional consumer registrar imports the event into the consumer queue without host wait; fixed caller-preallocated pool bounds resource reuse | native ABI/Python lifetime and negative tests; CuPy 14.2 on RTX 2060; dpnp 0.20/dpctl 0.22.1 on Arc B580 verifies kDLOneAPI pointer identity, Level Zero dependency barriers and capacity-one USM→VA→oneVPL AV1 backpressure | CUDA dependency, Intel Level Zero producer event, direct consumer-SYCL-queue dependency and USM pool/backpressure slices complete. Cross-context fault/pooled 30-minute soak, direct oneVPL USM consumption, processed-resource NVENC positive encode and built-wheel inspection remain pending |
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
into the common owned I420 representation. Sequence validation, decoder capability
checks, decoder creation and same-resolution reuse are isolated in
`nvdec_sequence`; mid-stream resolution changes remain explicitly unsupported.
Driver loading, CUDA context creation, parser callback wiring and successful
context detachment are isolated in `nvdec_runtime_setup`. Initialization errors
discard partial parser/context resources; the real-GPU test forces a post-runtime
input-open failure and then recreates a decoder successfully.
Parser destruction, mapped-frame release and decoder/context destruction are
isolated in `nvdec_runtime_cleanup`, with a CUDA context guard around each driver
operation. Decoder/context destruction remains deferred while an external GPU
frame lease exists; the real-GPU test releases that final lease and then recreates
and runs another decoder in the same process. `NvdecRuntimeOwner` now owns the API
table, parser, decoder and CUDA context across that deferred lifetime. Output
release callbacks retain only this owner instead of the complete decoder state;
it serializes mapping counts, destroys the parser at close, and destroys the
decoder/context after the final mapping is released.
The three NVDEC driver callbacks and their mutable state are isolated in
`NvdecCallbackState`. It owns resolution negotiation diagnostics, callback counts,
the exclusive CPU/GPU output-mode rule, bounded GPU pool and completed-frame
queues. The public decoder no longer exposes callback-owned CUDA mapping or queue
details; it selects an output mode, pumps packets and pops the completed frame.
CPU and GPU reads share one parser pump for incremental demux, timestamped packet
submission, the single EOS drain packet, callback diagnostics and CUDA context
release. Their public output-mode and frame-pool checks remain separate.
The CPU-output boundary is isolated in
`nvdec_cpu_output`: it owns both pitched CUDA 2D readbacks, NV12-to-I420 plane
separation and the mandatory mapped-frame unmap. The display callback therefore
only selects CPU/GPU output and enqueues the completed lease. Its GPU counterpart,
`nvdec_gpu_output`, constructs the common descriptor and CUDA native handle and
transfers the mapping into the bounded frame-pool lease without copying pixels.
The CPU path remains an intentional device-to-host copy path; the GPU-surface path
retains the mapped CUDA resource until its lease is released. The NVIDIA writer
converts supported 8-bit CPU inputs to NV12, submits one synchronous NVENC AV1
operation at a time, and immediately muxes the returned packet. Backend capability
and CUDA driver loading use the shared `DynamicLibrary` abstraction. `NvencApi`
owns both driver modules, resolves the required CUDA context functions and validates
the NVENC function table before an encode session can be created. `NvencSession`
then owns the encoder, input buffer, bitstream buffer and any internally created
CUDA context; externally supplied decode contexts remain borrowed. Session setup
contains the synchronous AV1 P4 constant-QP configuration, and one cleanup path is
used by close, flush/recreate and CPU-to-GPU input switching. GPU input submission
is isolated in `nvenc_gpu_submission`: CUDA pointer/array registration, mapping,
picture submission, unmapping and unregistering form one balanced scope without a
pixel copy. CPU input conversion is isolated in `nvenc_cpu_conversion`: it validates
planes and padded strides, converts BGR/RGB/BGRA/I420 to reusable contiguous NV12
staging storage, and makes the required host-to-NVENC-input copy boundary explicit.
Known-value tests cover padded NV12, I420 chroma interleaving and invalid stride
rejection without requiring NVIDIA hardware. `nvenc_cpu_submission` then owns the
pitched NVENC input-buffer lock, host upload, unlock and synchronous picture
submission, including balanced unlock on an invalid driver-returned layout.
`nvenc_packet_io` owns completed-bitstream lock/mux/unlock without an additional
packet copy and centralizes the EOS drain used by flush and close. A successful
bitstream lock is unlocked after both mux success and mux failure; empty
driver-returned buffers are rejected as codec failures.
`nvenc_gpu_frame_validation` normalizes CUDA pointer/array input after checking
backend, NV12 plane layout, pitch, dimensions, producer completion and native
handle consistency. GPU-independent tests cover the accepted pointer descriptor,
mismatched plane pitch and missing-completion rejection. Same-context enforcement
and CPU-owned/external-CUDA session transitions are centralized in
`NvencSessionManager`. It destroys native session resources before releasing the
first external-frame context owner, rejects CPU-to-GPU switching after submission,
and clears partial initialization resources. Encoder construction cleanup now also
handles driver-loading failure without dereferencing an absent API table.
Backend capability
rows are emitted
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
- oneVPL dispatcher/headers `2.17.0`; Intel GPU runtime 25.4/API 2.15,
  `mfxMemoryInterface 1.0` (external VA shared import verified on 2026-09-02)
- nv-codec-headers `n13.1.15.0`, source archive SHA-256 `2255bc74d038b95aa4be30f5f66322c2176acbdb90ada1851db6993536fbeaf7`
- Windows NVIDIA probe host: GeForce RTX 2060, compute capability 7.5, CUDA driver API 13.3, NVENC API 13.1
- Linux test host: `linux-machine`, GCC 13.3, CMake 3.28, Ninja
