# Implementation plan

仕様IDは外部仕様 `EXT`、受入条件 `AC`、内部仕様 `INT`、テスト `TEST` の順で
追跡する。詳細仕様書の移管後もこの規則を維持する。

## Phase 0: foundation

- CMakeによるWindows/Linux shared library build
- 例外を境界外へ出さないC ABI
- fixed-width型、`struct_size`、ABI version
- backend capability query
- CTest smoke test

## Phase 1: CPU VP9 vertical slice

- libvpx decode/encode
- libwebm demux/mux
- native capture/writer handles
- synchronous C ABI
- Python NumPy copy API
- WebM VP9 round-trip integration test

## Phase 1B: CPU AV1 vertical slice

- [x] SVT-AV1 8-bit encode with VP9-equivalent CPU input formats
- [x] libaom 8-bit decode with VP9-equivalent CPU output formats
- [x] WebM AV1 codec configuration and independent FFmpeg verification
- [x] ordered flush with sequence restart and bounded prefetch
- [ ] 10-bit public frame formats and SSIM gate

## Phase 2: asynchronous pipeline

- [x] bounded encode queue and reusable owned-frame buffer pool
- [x] blocking write / non-blocking try-write
- [x] ordered drain、flush、finalize
- [x] bounded prefetch decode
- [x] explicit encoder cancellation and bounded pipeline metrics

## Phase 3: hardware backends

- [x] oneVPL hardware loader/session and VP9/AV1 Query probe
- [x] Intel oneVPL VP9/AV1 encode, libwebm mux, C ABI/Python Writer on `linux-machine`
- [x] Intel oneVPL VP9/AV1 decode, libwebm demux, C ABI/Python Capture on `linux-machine`
- [ ] Windows D3D11 Intel hardware verification
- [x] Intel encoder multi-SyncPoint overlap and AsyncDepth 1/2/4/8 ordering tests
- [x] Intel decoder multi-SyncPoint overlap and AsyncDepth 1/2/4/8 ordering tests
- [x] Linux Intel decode surface→encoder direct shared-surface submission (no CPU Map/readback)
- [ ] Intel external D3D11/VA resource import and fully asynchronous shared-session pipeline
- [x] injected Intel pending-operation failure cleanup and session recovery
- [x] NVIDIA NVDEC VP9 public Capture（RTX 2060 VP9 positive、AV1 positive pending）
- [x] NVIDIA NVENC AV1 public Writer adapter and runtime capability gate
- [ ] NVIDIA AV1-capable GPU positive encode/decode verification
- [x] CPU fallbackと明示backend選択

## Phase 3B: performance and observability

- [x] public API end-to-end JSON benchmark and CI smoke
- [x] native aggregate backend/queue timing, high-water and copy-path metrics ABI
- [ ] conversion/codec/mux/GPU-event別の詳細stage timing
- [ ] approved 1080p/4K CPU/Intel/NVIDIA baseline and regression thresholds
- [x] strict Intel GPU transcode JSON benchmark and initial 1080p VP9 development baseline

## Phase 3C: failure recovery

- [x] bounded encoder queue failure propagation and waiter wakeup
- [x] Intel encode/decode pending SyncPoint cleanup and session recreation
- [ ] real driver reset/device removal qualification on Windows and Linux

## Phase 3D: common CPU/GPU frame interoperability

- [x] CPU immutable process-plan C ABI and Python Capture convenience API
- [x] CPU resize/crop/basic 8-bit conversion/rotate/flip/contain/cover subset
- [x] common `mkvc_gpu_frame` metadata, retain/release lease, generation and producer query/wait C ABI foundation
- [x] fixed-capacity generation-safe GPU frame pool with completion/lease-gated recycle and backpressure
- [ ] backend resource allocation/frame factories and consumer dependency registration integration
- [x] oneVPL SyncPoint and NVIDIA CUDA event completion adapter foundations
- [ ] connect completion adapters to decode/export/import/encode surface factories; add D3D11 fence/VA synchronization
- [x] oneVPL video-memory surface wrapper with SyncPoint, native handle, pool lease and deferred Surface Release
- [ ] Intel decode→external processing→resource import→encode shared-session pipeline (direct decode→encode is implemented on Linux)
- [x] Linux Intel oneVPL video-memory decode→`mkvc_decoder_read_gpu`→VA surface lease
- [x] Linux Intel VP9/AV1 direct decode-surface→encode transcode qualification
- [x] Python Intel `VideoCapture.read_surface()` descriptor/native-handle lease
- [x] Python Intel strict `require_gpu_resident` Capture/Writer path and actual copy-path metrics
- [ ] Windows Intel D3D11 `read_surface` hardware qualification
- [x] NVIDIA NVDEC CUDA pointer→NVENC registered-resource implementation
- [ ] NVIDIA NVDEC→NVENC positive AV1 hardware qualification and trace proof
- [x] backend-neutral borrowed native-handle descriptor and lease validation
- [x] connect Intel VA surface factory to real Linux decoder output
- [ ] connect Intel D3D11 texture export factory and external texture import/encoder surfaces
- [x] connect NVIDIA CUdeviceptr/context/pitch factory to mapped NVDEC surfaces
- [ ] NVIDIA CUarray/stream/event export and asynchronous dependency factory
- [x] native NVIDIA linear CUDA-pointer DLPack plane adapter and native lease deleter
- [x] Python stable-ABI DLPack capsule and `GpuFrame.plane()` protocol source API
- [ ] CuPy hardware qualification, consumer CUDA event dependency and Intel safe USM adapter
- [x] backend-neutral external GPU frame wrapper with producer-query and single-shot release callbacks; CUDA-pointer NV12 is NVIDIA-encode compatible
- [ ] external CUDA CUarray/DLPack import with native producer event/deleter ownership
- [ ] external D3D11 texture and VA surface oneVPL import adapter with producer fence/sync ownership
- [ ] strict GPU-resident policy and export/import edge copy-path trace
- [ ] Python CuPy/DLPack processed-resource import and hardware qualification
- [x] .NET GPU Frame SafeHandle, descriptor/native handle/wait and Capture.ReadSurface source API
- [x] .NET strict GPU-resident Capture/Writer and WriteSurface source API
- [x] .NET external GPU frame owner/query/release adapter
- [x] .NET 8 SDK-style build and Windows native-load/CPU round-trip smoke
- [x] header-only C++17 RAII wrapper over the stable C ABI
- [ ] .NET Intel/NVIDIA hardware smoke
- [ ] `TEST-GPU-001..020` lifetime, interop, fault, trace and soak qualification

## Phase 3E: CPU borrowed frame interoperability

- [x] C ABI CPU plane export descriptor and retained frame owner lease
- [x] Python `read_borrowed()` read-only NumPy view with propagated native owner lease
- [x] synchronous `write_borrowed()` for I420/NV12/packed layout (`queue_size=0` initial slice)
- [x] asynchronous `submit_borrowed()` submission lease, query/wait and mutation contract
- [x] fixed-capacity native input pool with backpressure, generation checks and Python NumPy views
- [ ] optional OS page-locked/pinned allocation mode and pinning metrics
- [x] .NET synchronous short pin and asynchronous unmanaged native-pool API
- [ ] .NET optional OS page-locked pool mode and pin-duration/GC metrics
- [ ] strict layout/stride/alignment validation and explicit copy-path fallback
- [ ] GPU decode→NumPy download/conversion edge tracing
- [ ] `TEST-CPUINT-001..007` lifetime, GC, fault, copy-path and soak qualification

## Phase 4: language bindings and distribution

- Python wheel
- [x] .NET 8 P/Invoke ABI layout/load smoke and SafeHandle foundation
- [ ] .NET high-level reader/writer/frame API and NuGet
- [x] dependency manifest、SPDX SBOM generator、source/artifact compliance gate foundation
- [x] hash-locked LICENSE/PATENTS collectorとTHIRD_PARTY_NOTICES生成
- [x] wheel/NuGetへのnative/legal/SBOM収録と実artifact gate
- [ ] project LICENSE確定、Windows artifact実build、release publication
