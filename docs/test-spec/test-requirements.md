---
document_type: test-specification
document_id: TEST-SYSTEM
status: proposed
profile: test-spec@1.0
---

# MKVCodec テスト要求

## 1. Test ID Catalog

### 1.1 Core / Container / Codec

| ID | Test requirement | Level | Environment |
|---|---|---|---|
| `TEST-SYS-001` | Windows x64/Linux x64でCPU build、load、version query | integration | Windows/Linux CI |
| `TEST-CONT-001` | VP9/AV1 MKV/WebMを独立toolでprobe/decodeしmetadataを照合 | integration | CPU CI |
| `TEST-CONT-002` | DocType、extension/container矛盾、WebM subset違反を検証 | unit/integration | CPU CI |
| `TEST-CONT-003` | PTS、duration、keyframe、長時間timestamp、display orderを検証 | integration | CPU/GPU CI |
| `TEST-CODEC-001` | libvpx VP9 encode→decode round-tripとPSNR/SSIM | integration | CPU CI |
| `TEST-CODEC-002` | SVT-AV1→libaom round-tripとPSNR/SSIM | integration | CPU CI |
| `TEST-CODEC-003` | H.264/HEVC列挙・指定・fallbackを拒否 | unit/integration | all CI |
| `TEST-CODEC-004` | invalid codec/backend組合せを初期化時に拒否 | unit | all CI |

### 1.2 External API

| ID | Test requirement | Level | Environment |
|---|---|---|---|
| `TEST-DEC-001` | read/read_bgr/read_nv12/read_surfaceの形式とEOS | integration | backend CI |
| `TEST-DEC-002` | iterator、context manager、idempotent close/release | integration | CPU CI |
| `TEST-DEC-003` | read_batchのsize、timeout、EOS | unit/integration | CPU CI |
| `TEST-DEC-004` | prefetch 0/1/4/16、queue上限、hit/miss | performance | CPU/GPU CI |
| `TEST-ENC-001` | BGR/RGB/BGRA/I420/NV12 shape/dtype/stride | unit/integration | CPU CI |
| `TEST-ENC-002` | write_batch、flush、遅延packet全回収 | integration | CPU/GPU CI |
| `TEST-ENC-003` | queue満杯block、try_write WOULD_BLOCK、cancel wakeup | concurrency | CPU CI |
| `TEST-ENC-004` | quality/vbr/cbr validationとbackend mapping | unit/integration | backend CI |
| `TEST-ENC-005` | odd size、unsupported format、non-contiguous/negative stride | unit | CPU CI |

### 1.3 Backend / GPU / Lifetime

| ID | Test requirement | Level | Environment |
|---|---|---|---|
| `TEST-BACK-001` | device/decoder/encoder capabilityがruntime queryと一致 | hardware | Intel/NVIDIA |
| `TEST-BACK-002` | auto selectionとdevice_preferenceの順序 | unit/hardware | all |
| `TEST-BACK-003` | GPUなし/driverなしでもloadし利用不可理由を返す | integration | CPU-only CI |
| `TEST-FRAME-001` | retain/release、double release、released access拒否 | unit | CPU CI |
| `TEST-FRAME-002` | consumer完了前にSurfaceがpool再利用されない | concurrency | Intel/NVIDIA |
| `TEST-FRAME-003` | device/context/format不一致を拒否または明示copy | hardware | Intel/NVIDIA |
| `TEST-ZC-001` | zero-copy traceでCPU round-trip/GPU copyなしを確認 | performance | supported GPU |
| `TEST-ZC-002` | require_zero_copy時にcopyへ降格しない | hardware | Intel/NVIDIA |
| `TEST-INTEL-001` | D3D11/VA-API oneVPL decode/encode/VPP | hardware | Windows/Linux Intel |
| `TEST-INTEL-002` | AsyncDepth 1/2/4/8、SyncPoint順序、device lost cleanup | hardware | Intel |
| `TEST-NV-001` | NVDEC VP9/AV1 decode | hardware | NVIDIA |
| `TEST-NV-002` | NVENC AV1 8-bit、対応時10-bit encode | hardware | NVIDIA |
| `TEST-NV-003` | CPU BGR upload→GPU convert→NVENC | hardware/perf | NVIDIA |
| `TEST-NV-004` | CUarray NVDEC→NVENC direct path | hardware/perf | supported NVIDIA |
| `TEST-NV-005` | NVENC VP9拒否、非対応世代のerror、driver reset cleanup | hardware | NVIDIA |

### 1.4 Frame Processing（将来対応）

| ID | Test requirement | Level | Environment |
|---|---|---|---|
| `TEST-PROC-001` | resize/crop/rotate/flip/letterbox/pillarboxの寸法、ROI、配置、背景をgolden imageと照合 | unit/integration | CPU/Intel/NVIDIA |
| `TEST-PROC-002` | NV12/P010/I420/RGB系変換とBT.601/709/2020、limited/full range、metadata伝播を検証 | integration | CPU/Intel/NVIDIA |
| `TEST-PROC-003` | 個別methodと融合`process`の結果が許容誤差内で一致し、中間surface数がbounded | integration/performance | CPU/Intel/NVIDIA |
| `TEST-PROC-004` | GPU-resident指定時にCPU readbackがなく、実経路がzero_copy/shared_surface/gpu_copyとしてtraceされる | hardware/trace | Intel/NVIDIA |
| `TEST-PROC-005` | unsupported補間・format・memory組合せとstrict copy制約を明示errorにし、黙ってfallbackしない | unit/hardware | all |

### 1.5 ABI / Language / Error

| ID | Test requirement | Level | Environment |
|---|---|---|---|
| `TEST-ABI-001` | C create/read-write/flush/destroyとstruct_size互換 | integration | Windows/Linux |
| `TEST-ABI-002` | null、不正handle/version、error code/detail | unit | Windows/Linux |
| `TEST-ABI-003` | C++ exceptionがABI外へ漏れない | fault injection | Windows/Linux |
| `TEST-PY-001` | Python exception、GC中frame lifetime、GIL解放 | integration | Python CI |
| `TEST-CS-001` | P/Invoke load、struct layout、SafeHandle/IDisposable | smoke | .NET CI |
| `TEST-ERR-001` | disk full、I/O error、cancel、timeout、device lost cleanup | fault injection | backend CI |
| `TEST-ERR-002` | close/release/destroyを反復・複数回実行 | stress | all CI |

### 1.6 Performance / Stability / Security

| ID | Test requirement | Level | Environment |
|---|---|---|---|
| `TEST-PERF-001` | 1080p30/60、4K30、対応時4K60 baseline | benchmark | CPU/Intel/NVIDIA |
| `TEST-PERF-002` | prefetch/async depth別throughput/latency curve | benchmark | CPU/Intel/NVIDIA |
| `TEST-PERF-003` | balanced pipelineにframe間overlapがある | trace | Intel/NVIDIA |
| `TEST-PERF-004` | 30分以上と数百回open/closeでRAM/VRAM/handleが増加しない | soak | backend CI |
| `TEST-PERF-005` | queue capacityに従いpeak memoryがbounded | stress | all CI |
| `TEST-SEC-001` | malformed container/packet/size overflow fuzz | fuzz | sanitizer CI |
| `TEST-SEC-002` | dynamic library searchが許可名/安全pathに限定 | unit/integration | Windows/Linux |

### 1.7 Packaging / Compliance

| ID | Test requirement | Level | Environment |
|---|---|---|---|
| `TEST-PKG-001` | wheel/NuGet/zipを展開しLICENSE/PATENTS/NOTICE/SBOMを確認 | release | packaging CI |
| `TEST-PKG-002` | vendor driver、SDK sample/stub、禁止binaryが未収録 | release | packaging CI |
| `TEST-PKG-003` | dependency version/source/license hashをallowlist照合 | release | packaging CI |
| `TEST-PKG-004` | H.264/HEVC symbol/GUID/config混入scan | release | packaging CI |
| `TEST-PKG-005` | endorsementを示唆するlogo/文言がない | review/scan | release CI |

## 2. Test Data

- 1 frame、短尺、GOP超え長尺
- 固定色、color bar、gradient、移動pattern、random noise
- 8-bit NV12/I420、対応時10-bit P010
- non-contiguous crop、negative stride view
- corrupted EBML、oversized element、truncated packet、invalid PTS
- 1080p/4K、30/60fps
- VP9/AV1の既知良好golden files

## 3. Independent Validation

生成物検証はMKVCodec自身だけで完結させない。少なくとも独立したprobe/decoder、Matroska validator/test suite、複数の一般的player/decoderを使用する。

## 4. Pass Criteria

1. 全mandatory testがPASSする。
2. hardware非搭載時はskipだけでなく「利用不可を正しく報告するtest」がPASSする。
3. crash、hang、data race、use-after-free、leakがない。
4. performanceは承認baselineから理由のない重大回帰がない。
5. release artifact compliance testがすべてPASSする。

