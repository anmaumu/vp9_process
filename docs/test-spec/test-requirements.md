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
| `TEST-ENC-003` | queue満杯block、try_write WOULD_BLOCK、cancelがproducer/flush/submissionを起床しqueued workだけをcancel terminalへ遷移する | concurrency | CPU CI |
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
| `TEST-INTEL-001` | D3D11/VA-API oneVPL decode/export/import/encode | hardware | Windows/Linux Intel |
| `TEST-INTEL-002` | AsyncDepth 1/2/4/8、SyncPoint順序、device lost cleanup | hardware | Intel |
| `TEST-NV-001` | NVDEC VP9/AV1 decode | hardware | NVIDIA |
| `TEST-NV-002` | NVENC AV1 8-bit、対応時10-bit encode | hardware | NVIDIA |
| `TEST-NV-003` | CPU BGR upload→GPU convert→NVENC | hardware/perf | NVIDIA |
| `TEST-NV-004` | CUarray NVDEC→NVENC direct path | hardware/perf | supported NVIDIA |
| `TEST-NV-005` | NVENC VP9拒否、非対応世代のerror、driver reset cleanup | hardware | NVIDIA |
| `TEST-GPU-001` | retain/release/export lease中のnative resource非再利用とreleased/generation不一致access拒否 | concurrency/sanitizer | Intel/NVIDIA |
| `TEST-GPU-002` | producer/複数consumer completion順序を全順列で変え、最後のcompletion後だけpoolへ戻る | deterministic concurrency | Intel/NVIDIA |
| `TEST-GPU-003` | CUDA context/device、D3D11 device、VA display不一致と別process handle使用を拒否 | negative/hardware | Windows/Linux GPU |
| `TEST-GPU-004` | device-wide syncなしでconsumer stream/fence dependencyが正しく待機する。VA adapterはfake ABIでtimeout_ns=0、pending→timeout→ready、success/failure固定、UNIMPLEMENTED/一般error、library寿命を検証する。実機native importはsurface単位同期とencodeを確認するが、pending実負荷や独立traceの証明とは分ける | unit/hardware/trace/race | Intel/NVIDIA |
| `TEST-GPU-005` | NV12/P010のplane offset、pitch、alignment、D3D11 subresource、VA surface、CUDA pointer/CUarray descriptorをguard付き検証 | hardware | Intel/NVIDIA |
| `TEST-GPU-006` | Intel oneVPL decode→native export→外部resource import→encodeをCPU Mapなしで実行し、PTS/order/golden decodeを検証 | end-to-end/trace | Windows D3D11/Linux VA-API Intel |
| `TEST-GPU-007` | NVIDIA NVDEC→NVENCをDtoH/HtoDなしで実行し、map/register/unmap lifetime、PTS/order/golden decodeを検証 | end-to-end/trace | NVIDIA |
| `TEST-GPU-008` | Intel/NVIDIA decode→export→外部GPU処理→import→encodeでformat、色metadata、PTS、layoutを検証 | end-to-end | Intel/NVIDIA |
| `TEST-GPU-009` | DLPackをconsumer stream付きでCuPy等へ渡し、producer dependency、shape/stride/device/deleterを検証する。native CUDA event→stream挿入と連続NV12 import ownershipは実機/常時CIで独立検証する | Python/hardware | Intel対応環境/NVIDIA |
| `TEST-GPU-010` | Python objectを先にGC、DLPack consumerを先に解放、循環参照、interpreter shutdownの各順序でcrash/leakなし | stress/subprocess | Python GPU CI |
| `TEST-GPU-011` | `require_gpu_resident`、`allow_gpu_copy`、`allow_cpu_copy`の組合せ表どおり成功/失敗し、silent fallbackがない | parameterized | all |
| `TEST-GPU-012` | decode/export/import/encode各stageのdevice lost、timeout、cancelで全waiterが起床し一度だけcleanupされる | fault injection | Intel/NVIDIA |
| `TEST-GPU-013` | API traceとCUDA/oneVPL/OS traceでoperation別copy-pathを照合し、CPU transfer counterがzero | trace/performance | Intel/NVIDIA |
| `TEST-GPU-014` | pool枯渇、遅いconsumer、複数stream、30分soakでdeadlockせずVRAM/handle/pending数がbounded | stress/soak | Intel/NVIDIA |
| `TEST-GPU-015` | Linux VA Intel VP9/AV1 decode surfaceを`write_surface`へ渡し、lease即時解放、pool進行、frame数一致を検証 | end-to-end/hardware | Linux Intel |
| `TEST-GPU-016` | `require_gpu_resident=True`でCPU read/writeを拒否し、GPU transcode metricsが`zero_copy`となる | API/end-to-end | Python/Linux Intel |
| `TEST-GPU-017` | .NET strict Capture `ReadSurface`→Writer `WriteSurface`でlease解放順、frame数、PTS、`zero_copy` metricsを検証する | end-to-end/hardware | .NET Intel/NVIDIA |
| `TEST-GPU-018` | C ABI copy policyのsize/version/conflict、初回frame後変更、queue/prefetch制約を検証 | ABI/unit | CPU/Intel |
| `TEST-GPU-019` | CUDA pointer/CUarray、D3D11 texture、VA surface importのdevice/layout/completionを検証し、encode完了後だけrelease callbackを一度呼ぶ。mock resourceでcallback順序・invalid layoutを常時CI検証し、CUDA eventと実CUarrayはcontextをpopした状態からquery/waitする。Intelはmemory interface 1.0/1.1を受理し、未知major/null functionを拒否するunit回帰を行う。same-display/video-memory shared import実機で8 frames、flush/rebind、producer待機、別display拒否、最初のownerのflush/closeまでの保持、CPU再decodeのPTS/count/PSNRを検証する。通常CIではcapability不足をskipし、`MKVC_REQUIRE_INTEL_EXTERNAL_IMPORT=1`では失敗させる | unit/hardware/lifetime | common/Intel/NVIDIA |
| `TEST-GPU-020` | Python importでnative owner holderのGC寿命、producer stream/event dependency、DLPack deleter所有権、未消費capsule、cancel/failure時cleanupを検証。同期済みCUDA pointer/VA surface owner寿命とVA ID範囲・import失敗時holder解放は常時CI対象。native CUDA-event importはNVIDIA hardware対象。Linux native VA Python経路はcapture先行close、4 framesのstrict encode、writer保持中owner生存・close後GC、CPU再decodeのcount/shape/画素変化を実機検証する | Python/unit/hardware | common/NVIDIA/対応Intel |

`TEST-GPU-019`のVA native同期補足: `mkvc_intel_va_surface_sync`はnative VA importで
8 frames、flush/rebind、device/display不一致、owner解放順、CPU再decodeのPTS/count/PSNRを
確認する。`mkvc_gpu_frame`はquery非null、無効surface、非Intel buildでの拒否と
失敗時release callback非実行を検証する。surface ID 0は有効。symbol欠落/driver未実装では
blocking fallbackしない。driver未実装はfakeで検証し、symbol欠落の動的loader fault注入は
後続項目とする。実機試験は完了済みdecoder surfaceを用いるので、VA producerの実負荷での
pending/race試験、OpenCL/SYCL kernel処理、外部API traceは別途必要。通常CIはcapability
不足をskipし、`MKVC_REQUIRE_INTEL_EXTERNAL_IMPORT=1`ではnative/Pythonとも失敗させる。

GPU補足（TEST-GPU-003/004/006/014/019/020）:

- `mkvc_d3d11_fence`: Windows hardwareでpending/timeout/recovery、同じadapter上の別device拒否、寸法/target/subresource不正、GPU CopyResourceのidentity/画素、caller-first COM解放、128回のowner single-releaseを確認する。長めの反復は実行引数4096で行う。MKVC_REQUIRE_D3D11_FENCE=1ではcapability不足を失敗にする。CPU readbackは試験oracleだけで行い、製品import内には持ち込まない。fake completionではdevice lossとterminal固定を検証する。
- `mkvc_intel_import_contract`: runtime refcount>1、Locked>0、GetRefCounter不足/失敗でretireしないことをunit検証する。保持上限64のfault注入は追加検証項目。
- `mkvc_python_intel_opencl_roundtrip`: Linux Intel decode→外部OpenCL luma反転/UV中立化→VA共有import→VP9 encode→CPU oracle。既定32 frames、MKVC_OPENCL_TEST_FRAMESで最大10000へ延長する。PTS/count/Y-PSNR>25 dB、import先が別surface・同じdisplay、owner先行GC、runtime入力参照の保持、終了時全owner解放、最大保持数<=65を検証する。CPU oracleは比較用の別経路であり、GPU処理中のhost転送と区別する。OpenCL側はshared image acquire→kernel→release→clFinishで明示同期する。独立driver trace、VRAM/RSSの長時間計測、30分soak、USM変換はこの試験の成功だけでは受入れ完了としない。

### 1.4 CPU Frame Interoperability

| ID | Test requirement | Level | Environment |
|---|---|---|---|
| `TEST-CPUINT-001` | borrowed NumPyのpointer identity、shape/stride、read-only属性、PTSをnative descriptorと照合する | unit/integration | Python CPU CI |
| `TEST-CPUINT-002` | frameを先にcloseしてもview lease中はslotを再利用せず、最後のview解放後にだけ再利用する | lifetime/concurrency | Python CPU CI |
| `TEST-CPUINT-003` | sync borrowed encodeがreturnまでinputを読み終え、return後の再利用・mutationが安全である | integration/race | CPU CI |
| `TEST-CPUINT-004` | async borrowed submissionが成功/失敗/cancelのcompletionまでownerを保持し、一度だけ解放する | concurrency/fault | Python/.NET/native CI |
| `TEST-CPUINT-005` | native/pinned poolが固定容量、backpressure、generation規則を満たし、pool owner先行破棄・view/submission lease後も安全で、長時間.NET GC pinningとmemory増加がない | stress/performance | native/Python/.NET CPU CI |
| `TEST-CPUINT-006` | GPU decode→NumPyは`cpu_readback`、CPU borrowed共有は`zero_copy`となり、形式変換allocationをedge別traceする | trace/hardware | CPU/Intel/NVIDIA |
| `TEST-CPUINT-007` | plane count、dtype、shape、stride、alignment不一致がstrict時に失敗し、copy許可時だけcopyする | parameterized | CPU CI |

### 1.5 CPU Convenience Processing

| ID | Test requirement | Level | Environment |
|---|---|---|---|
| `TEST-PROC-001` | CPU resize/crop/rotate/flip/letterbox/pillarboxの寸法、ROI、配置、背景をgolden imageと照合 | unit/integration | CPU CI |
| `TEST-PROC-002` | CPU NV12/P010/I420/RGB系変換とBT.601/709/2020、limited/full range、metadata伝播を検証 | integration | CPU CI |
| `TEST-PROC-003` | 個別methodと`process`の結果が許容誤差内で一致し、CPU中間allocation数がbounded | integration/performance | CPU CI |
| `TEST-PROC-004` | GPU frameへconvenience processingを要求するとinterop APIを示すunsupported errorを返し、CPU fallbackしない | API/hardware | Intel/NVIDIA |
| `TEST-PROC-005` | unsupported補間・format・memory組合せを明示errorにし、黙ってfallbackしない | unit | CPU/all |

### 1.6 ABI / Language / Error

| ID | Test requirement | Level | Environment |
|---|---|---|---|
| `TEST-ABI-001` | C create/read-write/flush/destroyとstruct_size互換 | integration | Windows/Linux |
| `TEST-ABI-002` | null、不正handle/version、error code/detail | unit | Windows/Linux |
| `TEST-ABI-003` | C++ exceptionがABI外へ漏れない | fault injection | Windows/Linux |
| `TEST-PY-001` | Python exception、GC中frame lifetime、GIL解放 | integration | Python CI |
| `TEST-CS-001` | P/Invoke load、struct layout、SafeHandle/IDisposable | smoke | .NET CI |
| `TEST-ERR-001` | disk full、I/O error、cancel、timeout、device lost cleanup | fault injection | backend CI |
| `TEST-ERR-002` | close/release/destroyを反復・複数回実行 | stress | all CI |

### 1.7 Performance / Stability / Security

| ID | Test requirement | Level | Environment |
|---|---|---|---|
| `TEST-PERF-001` | 1080p30/60、4K30、対応時4K60 baseline | benchmark | CPU/Intel/NVIDIA |
| `TEST-PERF-002` | prefetch/async depth別throughput/latency curve | benchmark | CPU/Intel/NVIDIA |
| `TEST-PERF-003` | balanced pipelineにframe間overlapがある | trace | Intel/NVIDIA |
| `TEST-PERF-004` | 30分以上と数百回open/closeでRAM/VRAM/handleが増加しない | soak | backend CI |
| `TEST-PERF-005` | queue capacityに従いpeak memoryがbounded | stress | all CI |
| `TEST-SEC-001` | malformed container/packet/size overflow fuzz | fuzz | sanitizer CI |
| `TEST-SEC-002` | dynamic library searchが許可名/安全pathに限定 | unit/integration | Windows/Linux |

### 1.8 Packaging / Compliance

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
