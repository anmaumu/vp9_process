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
- [ ] explicit cancellation、pipeline metrics

## Phase 3: hardware backends

- [x] oneVPL hardware loader/session and VP9/AV1 Query probe
- [x] Intel oneVPL VP9/AV1 encode, libwebm mux, C ABI/Python Writer on `linux-machine`
- [x] Intel oneVPL VP9/AV1 decode, libwebm demux, C ABI/Python Capture on `linux-machine`
- [ ] Windows D3D11 Intel hardware verification
- [x] Intel encoder multi-SyncPoint overlap and AsyncDepth 1/2/4/8 ordering tests
- [x] Intel decoder multi-SyncPoint overlap and AsyncDepth 1/2/4/8 ordering tests
- [ ] Intel zero-copy surfaces
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

## Phase 3C: failure recovery

- [x] bounded encoder queue failure propagation and waiter wakeup
- [x] Intel encode/decode pending SyncPoint cleanup and session recreation
- [ ] real driver reset/device removal qualification on Windows and Linux

## Phase 3D: common GPU-resident frame processing

- [x] CPU immutable process-plan C ABI and Python Capture convenience API
- [x] CPU resize/crop/basic 8-bit conversion/rotate/flip/contain/cover subset
- [x] common `mkvc_gpu_frame` metadata, retain/release lease, generation and producer query/wait C ABI foundation
- [x] fixed-capacity generation-safe GPU frame pool with completion/lease-gated recycle and backpressure
- [ ] backend resource allocation/frame factories and consumer dependency registration integration
- [x] oneVPL SyncPoint and NVIDIA CUDA event completion adapter foundations
- [ ] connect completion adapters to decode/VPP/encode surface factories; add D3D11 fence/VA synchronization
- [x] oneVPL video-memory surface wrapper with SyncPoint, native handle, pool lease and deferred Surface Release
- [ ] Intel decode→VPP→encode shared-surface pipeline
- [ ] NVIDIA NVDEC→NVENC registered-resource pipeline
- [x] backend-neutral borrowed native-handle descriptor and lease validation
- [ ] connect Intel D3D11 texture/VA surface factories to real decoder/VPP surfaces
- [ ] connect NVIDIA CUdeviceptr/CUarray/context/stream/event factories to mapped NVDEC surfaces
- [ ] Intel safe USM path and NVIDIA CUDA DLPack adapters
- [ ] NVIDIA NPP/CUDA mapping
- [ ] Intel oneVPL VPP/D3D11/VA-API mapping
- [ ] fused processing, strict GPU-resident policy and copy-path trace
- [ ] Python GPU Frame/DLPack/CuPy processing surface
- [ ] C++/.NET GPU Frame and native handle surface
- [ ] `TEST-GPU-001..014` lifetime, interop, fault, trace and soak qualification

## Phase 4: language bindings and distribution

- Python wheel
- [x] .NET 8 P/Invoke ABI layout/load smoke and SafeHandle foundation
- [ ] .NET high-level reader/writer/frame API and NuGet
- [x] dependency manifest、SPDX SBOM generator、source/artifact compliance gate foundation
- [x] hash-locked LICENSE/PATENTS collectorとTHIRD_PARTY_NOTICES生成
- [x] wheel/NuGetへのnative/legal/SBOM収録と実artifact gate
- [ ] project LICENSE確定、Windows artifact実build、release publication
