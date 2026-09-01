---
document_type: internal-specification
document_id: INT-SYSTEM
status: proposed
profile: internal-spec@1.1
---

# MKVCodec 内部仕様書

> 本文書は外部仕様を実現するArchitecture、処理、制約および検証方法を定義する。

## 1. 関連外部仕様

Status: `CONFIRMED`

`docs/external-spec/system-spec.md`の全`EXT-*`を対象とする。対応関係は`docs/traceability.md`を正とする。

## 2. Architecture / Components

Status: `PROPOSED`

- `INT-ARCH-001`: C++ Coreを唯一の処理本体とし、Python/C# bindingは薄く保つ。
- `INT-ARCH-002`: 公開境界をC ABIとし、C++ ABI/STL/exceptionを越境させない。
- `INT-ARCH-003`: container、codec backend、CPU convenience conversion、frame import/export、memory interop、queue、statisticsを分離する。GPU画像処理algorithmはCoreへ実装しない。
- `INT-ARCH-004`: GPU adapterはoptional moduleとし、GPUなしでもCoreをload可能にする。
- `INT-ARCH-005`: component依存方向をBinding → C ABI → Core → Interface → Backendとし、逆依存を禁止する。

```text
Python / C#
    |
    v
C ABI
    |
    v
VideoCapture / VideoWriter
    +-- IDemuxer / IMuxer (libwebm)
    +-- IVideoDecoder
    +-- IVideoEncoder
    +-- FrameInterop / ImportExport
    +-- optional CPU PixelConverter
    +-- DeviceRegistry / CapabilityRegistry
    +-- FramePool / BitstreamPool / BoundedQueue
    `-- Statistics / Trace
```

Backend:

```text
CPU:     libvpx VP9, SVT-AV1 encode, libaom AV1 decode, libyuv
Intel:   oneVPL + D3D11 (Windows) / VA-API (Linux)
NVIDIA:  NVDEC/NVENC + CUDA, optional D3D11 interop
```

## 3. Internal Interfaces / Data Flow

Status: `PROPOSED`

- `INT-IF-001`: decoder/encoderはbackend差を`Submit/TryReceive/Flush`で吸収する。
- `INT-IF-002`: sync backendは専用workerで同じsubmit/receive modelへ適合する。
- `INT-IF-003`: muxerはcodec backendに依存せず`EncodedPacket`のみを受け取る。
- `INT-IF-004`: encoderはcontainerに依存せず`VideoFrame`を受けて`EncodedPacket`を返す。
- `INT-IF-005`: capability queryはsession生成前に実施し、無効な組合せを拒否する。

```cpp
class IVideoDecoder {
 public:
  virtual SubmitResult Submit(const EncodedPacket&) = 0;
  virtual ReceiveResult TryReceive(VideoFrame*) = 0;
  virtual void Flush() = 0;
};

class IVideoEncoder {
 public:
  virtual SubmitResult Submit(const VideoFrame&) = 0;
  virtual ReceiveResult TryReceive(EncodedPacket*) = 0;
  virtual void Flush() = 0;
};
```

Decode flow:

```text
libwebm demux -> EncodedPacket -> decoder submit -> completion/reorder
-> VideoFrame lease -> format-specific external API
```

Encode flow:

```text
external frame -> validate import -> retain owner/completion -> encoder submit
-> completion -> EncodedPacket -> libwebm mux -> finalize
```

明示的なCPU convenience APIまたはcodec入力layoutと不一致のcopy許可経路だけが形式変換を行う。GPU processingは外部libraryが行い、Coreはそのresourceとproducer completionをimportする。

## 4. Data Model / Invariants

Status: `PROPOSED`

- `INT-DATA-001`: timestamp/durationはsigned `int64_t` nanosecondsで保持する。
- `INT-DATA-002`: `EncodedPacket`はdata、PTS、duration、keyframe、codecを持つ。
- `INT-DATA-003`: `VideoFrame`はCPU/D3D11/VA-API/CUDAのtagged representationとする。
- `INT-DATA-004`: CPU planeはdata pointer、size、strideを持ち、visible widthとallocation pitchを区別する。
- `INT-DATA-005`: 公開C structは先頭に`struct_size`とABI versionを持つ。
- `INT-DATA-006`: width/height、pixel format、plane count、stride、device/context互換性を投入前に検証する。
- `INT-DATA-007`: frame/packetのPTSはmuxまで失わない。decode orderとdisplay orderを区別する。
- `INT-DATA-008`: released frameのnative handle/planeへaccessできない。

```cpp
struct EncodedPacket {
  Buffer data;
  int64_t pts_ns;
  int64_t duration_ns;
  bool keyframe;
  Codec codec;
};

using VideoFrame = std::variant<CpuFrame, D3D11Frame, VaapiFrame, CudaFrame>;
```

Invariant:

1. queue size ≤ configured capacity。
2. pool resourceはfree、submitted、in-use-by-consumerのいずれか1状態にある。
3. GPU完了前のSurfaceをfree poolへ戻さない。
4. mux済みpacketのPTSはcontainer規則を満たす。
5. `close`完了後に新規submitを受理しない。

## 5. Normal and Failure Sequences

Status: `PROPOSED`

### 5.1 CPU VP9 encode

```text
write(BGR)
-> validate ndarray
-> copy to library buffer
-> libyuv BGR-to-I420
-> libvpx submit/get packet
-> libwebm add frame
-> release input slot
```

### 5.2 Intel GPU transcode

```text
demux -> DecodeFrameAsync -> SyncPoint
-> decoded D3D11/VA Surface lease
-> external export -> external processing -> resource import
-> EncodeFrameAsync -> SyncPoint
-> packet -> mux
```

### 5.3 NVIDIA zero-copy transcode

```text
demux -> NVDEC direct output CUarray
-> same CUDA context/ordered stream
-> register/map CUarray in NVENC
-> NVENC AV1 -> compressed packet host readback
-> mux
```

### 5.4 Failure

```text
detect error
-> stop accepting input
-> cancel/wake bounded queues
-> drain safe completions or abort backend
-> unmap/unregister resources
-> release frame/bitstream pools
-> best-effort mux finalize
-> publish stable error
```

## 6. State Model

Status: `PROPOSED`

- `INT-STATE-001`: capture/writerは`CREATED → RUNNING → FLUSHING → CLOSED`を基本状態とする。
- `INT-STATE-002`: failure時は`RUNNING/FLUSHING → FAILED → CLOSED`とする。
- `INT-STATE-003`: `close`はどの非終端状態からも呼べ、冪等である。
- `INT-STATE-004`: frame leaseは`AVAILABLE → SUBMITTED → EXTERNAL_LEASED → RECYCLABLE`を取る。

禁止遷移:

- `CLOSED`から`RUNNING`
- `FAILED`後の新規write/read submit
- GPU completion前の`RECYCLABLE`

## 7. Error Handling / Retry / Cleanup

Status: `PROPOSED`

- `INT-ERR-001`: C ABI entryは`noexcept`とし全C++ exceptionを捕捉する。
- `INT-ERR-002`: error categoryをinvalid argument、unsupported、would block、timeout、backend、I/O、device lost、EOSへ正規化する。
- `INT-ERR-003`: detail messageはthread-local storageへ保存し、次のsame-thread API callまで有効とする。
- `INT-ERR-004`: `destroy/release/close`を冪等にする。
- `INT-ERR-005`: destructorはexceptionを外へ出さない。
- `INT-ERR-006`: GPU device lost時は全pending slotをfailedにし、waiterを起床する。
- `INT-ERR-007`: disk full/I/O errorでは以後のwriteを拒否し、可能な範囲でcontainerを閉じる。
- `INT-ERR-008`: codec/backendを別codecへretry/fallbackしない。`auto`のsession生成前探索のみ許可する。

## 8. Concurrency / Resource Lifetime

Status: `PROPOSED`

- `INT-PIPE-001`: 全queueをboundedにし、既定overflowはblockとする。
- `INT-PIPE-002`: decode workerとencode workerを基本とし、libvpx内部threadとの過剰並列を避ける。
- `INT-PIPE-003`: GPU backendは複数SyncPoint/event/slotを保持し、submit直後の全面waitを避ける。
- `INT-PIPE-004`: frame leaseはatomic reference countとbackend completionの両方が成立した時だけpoolへ返す。
- `INT-PIPE-005`: queue close/cancelは全block中threadを起床する。
- `INT-PIPE-006`: safe既定APIはlibrary bufferへ同期copyしてからGILを解放する。明示borrowed APIだけがowner referenceをcompletionまで保持する。
- `INT-PIPE-007`: long-running native処理、queue wait、GPU waitではGILを解放する。
- `INT-PIPE-008`: borrowed NumPy入力を非同期submitする場合はcompletion tokenまでPython owner referenceを保持し、完了前のmutationを契約違反とする。

Mode初期値:

| Mode | Prefetch | Async depth | Queue | Backend mapping |
|---|---:|---:|---:|---|
| low_latency | 1-2 | 1-2 | 1-2 | lag/lookahead 0 |
| balanced | 4 | 4 | 8 | 標準 |
| throughput | 8-16 | 6-8 | 16 | 深いpipeline |

CPUではasync depthをworker queue、codec internal threads、lagへ読み替える。

## 9. Backend Design Rules

Status: `PROPOSED`

### 9.1 Container

- `INT-CONT-001`: libwebmをIDemuxer/IMuxer adapterで包む。
- `INT-CONT-002`: `.webm`はDocType `webm`、`.mkv`は`matroska`とする。
- `INT-CONT-003`: VP9/AV1 CodecID、codec configuration、keyframe、PTS/durationを正しく設定する。
- `INT-CONT-004`: closeでは全codec packet回収後にSegmentをfinalizeする。

### 9.2 CPU

- `INT-CPU-001`: VP9 encode/decodeはlibvpx、AV1 encodeはSVT-AV1、AV1 decodeはlibaomを使う。
- `INT-CPU-002`: BGR/RGB/BGRA conversionはlibyuv、I420/NV12入力は可能なら変換を省略する。
- `INT-CPU-003`: 1 stream原則1 application workerとしcodec内部threadを設定する。
- `INT-CPU-004`: reusable buffer poolを使い、per-frame allocationを通常経路から除く。
- `INT-CPU-005`: flush時にcodec内の遅延packetを全回収する。

### 9.3 Intel oneVPL

- `INT-INTEL-001`: WindowsはD3D11、LinuxはVA-API memoryを使用する。
- `INT-INTEL-002`: `DecodeFrameAsync/EncodeFrameAsync`と複数SyncPointを使用する。
- `INT-INTEL-003`: RGB変換、resize等は外部VPP/D3D11/VA-API/OpenCL/SYCL libraryの責務とし、Coreは処理済みresourceのidentity、layout、completionを検証してimportする。
- `INT-INTEL-004`: decode/encode deviceとimport Surfaceが互換ならzero-copy、同GPUで別allocationを要する場合は明示`gpu_copy`、CPU経由は明示`cpu_readback/cpu_upload`とする。
- `INT-INTEL-005`: CPU plane access時のみsync/map/copyする。

### 9.4 NVIDIA

- `INT-NV-001`: `nv-codec-headers`を使用したclean implementationとしSDK sample/base classを取り込まない。
- `INT-NV-002`: driver API DLL/SOを動的loadし、不在を利用不可capabilityとして扱う。
- `INT-NV-003`: NVDECはVP9/AV1、NVENCはAV1のみ公開する。
- `INT-NV-004`: input/output slotをringとして再利用する。
- `INT-NV-005`: CUDA stream/eventまたはD3D同期を使い、context全体同期を通常経路で避ける。
- `INT-NV-006`: 対応時NVDEC application-provided block-linear CUarrayをNVENCへ直接登録する。
- `INT-NV-007`: libwebmへ渡す圧縮packetのみhostへ回収する。

### 9.5 CPU Convenience Processing

- `INT-PROC-001`: CPU owned frameのresize、crop、色変換、rotate/flip、letterbox/pillarboxを`FrameProcessPlan`へ正規化する。
- `INT-PROC-002`: CPU implementationだけをlibyuv等へmappingし、NVIDIA NPP/CUDAおよびIntel VPP/shader kernelは実装しない。
- `INT-PROC-003`: 入力frameをin-place変更せず、CPU出力bufferはbounded poolからleaseする。
- `INT-PROC-004`: pixel format、chroma subsampling、color primaries、transfer、matrix、range、PTSを処理前後で検証・伝播する。
- `INT-PROC-005`: crop alignment、fit、letterbox/pillarbox配置、background、rotate後寸法を決定論的な共通幾何規則で計算する。
- `INT-PROC-006`: capability queryでCPU処理、format、補間方式を事前確認し、GPU frame入力はinterop APIを示すunsupported errorとする。
- `INT-PROC-007`: CPU処理の中間allocationを抑制するが、処理結果をzero-copy/shared-surfaceとは報告しない。
- `INT-PROC-008`: C ABIはopaque CPU frame/process handleを使用し、Python/C++/C# bindingが同じCPU処理planと実行結果を共有する。

GPU処理は外部libraryに委ねる。Coreは処理前resourceのexportと処理後resourceのimportだけを担当し、外部kernelの処理時間や内部copyをCore自身のzero-copyとして推測しない。

### 9.6 GPU Frame、Lease、Interop（実装予定）

Source layoutは共通所有権・同期を`src/gpu/`、将来のvendor実装を
`src/gpu/intel/`と`src/gpu/nvidia/`、Python/DLPack adapterをbinding側へ分離する。
既存backendは段階的に移行し、同時の全面renameでreview範囲を広げない。

- `INT-GPU-001`: `mkvc_gpu_frame`は`GpuFrameCore`へのopaque C handleとし、backend固有objectをABI structへ直接埋め込まない。
- `INT-GPU-002`: `GpuFrameCore`はsurface resource、immutable metadata、device identity、producer completion、atomic external lease count、pool generationを保持する。
- `INT-GPU-003`: pool再利用条件を`producer complete && consumer completion complete && external lease count == 0`とし、generation不一致handleを拒否する。
- `INT-GPU-004`: completion backendはIntel SyncPoint/D3D11 fence/VA sync、NVIDIA CUDA eventを共通query/wait/dependency interfaceへadapterする。
- `INT-GPU-005`: Intel pipelineはdecode surfaceをexternal lease中retainし、外部処理済みD3D11/VA-API resourceをproducer completion後にoneVPL encoderへ渡してencode completionまでretainする。external importはsessionの`mfxMemoryInterface >= 1.1`を実行時確認し、`MFX_SURFACE_FLAG_IMPORT_SHARED`だけを要求する。interface/function不足、`IMPORT_COPY`、unsupported応答ではCPU Map/copyへ降格せず`NOT_SUPPORTED`とする。CPU Mapは明示CPU export時だけ許可する。
- `INT-GPU-006`: Intel native exportはWindowsで`ID3D11Texture2D* + subresource`、Linuxで`VADisplay + VASurfaceID`を返し、AddRef/Releaseまたはlease lifetime規則をplatform別に固定する。
- `INT-GPU-007`: Intel external consumerとの同期はD3D11 fence/keyed mutexまたはoneVPL/VA completionを明示し、暗黙の同時accessを許可しない。
- `INT-GPU-008`: NVIDIA pipelineはmapped NVDEC CUarray/device viewをcompletion付きslotとしてexportし、外部処理済みCUDA resourceをNVENC registered resourceへ登録する。unmap/unregisterは全consumer完了後に行う。
- `INT-GPU-009`: NVIDIA exportはCUDA primary/owned context identity、device ordinal、CUdeviceptr/CUarray、pitch、plane offset、producer CUDA eventを返し、別context pointerの誤使用を拒否する。
- `INT-GPU-010`: DLPack producerはmanaged tensorのdeleterへGPU frame leaseを保持させ、`__dlpack__(stream=...)`でconsumer streamがproducer completionを待つdependencyを挿入する。CUDA eventがあるlinear pointerでは指定contextを一時pushし、`cuStreamWaitEvent`でhost/device-wide waitなしにdependencyを設定する。DLPack importはcapsuleを一度だけconsumeし、元deleterをimport frameの最終leaseまで保持する。
- `INT-GPU-011`: Intel USM/DLPackは実memoryがUSM pointerとして安全に表現できる経路だけを公開し、D3D11 texture/VA surfaceを偽のlinear pointerとして公開しない。非対応時はnative surface APIを使用させる。
- `INT-GPU-012`: Python wrapperはGPU待機中GILを解放し、GC/finalizerは例外を出さず、interpreter shutdown後にPython APIへcallbackしない。
- `INT-GPU-013`: copy-path recorderは各edgeを`shared_surface/zero_copy/gpu_copy/cpu_upload/cpu_readback`として実測記録し、要求値から推測しない。
- `INT-GPU-014`: device lost/cancel/timeout時は全completionをterminal failureへ遷移させ、waiterを起床し、resourceを依存順に一度だけ解放する。
- `INT-GPU-015`: copy policyは既存create configのABI sizeを変更せずversioned setterで設定し、最初のframe受理後の変更を拒否する。strict指定時はCPU read/write APIをbackend呼出前に拒否する。
- `INT-GPU-016`: GPU import descriptorはresource owner、memory type/layout、device/context、producer completion、release callbackを保持し、validation失敗時を含めcallbackを高々一度だけ呼ぶ。
- `INT-GPU-017`: encoder submissionはproducer dependencyをdevice-wide同期なしで待ち、encode completionまでimport ownerをretainする。consumer completion後にのみrelease callbackを実行する。外部CUDA event importはCUDA Driver APIをruntime loadし、指定contextをquery中だけpush/popして`cuEventQuery`を共通completionへ変換する。CUDA runtime callback、Python callback、`cuCtxSynchronize`は使用しない。

所有権の基準シーケンス:

```text
pool slot lease -> producer submit -> producer completion
                -> zero or more consumer leases/dependencies
                -> all consumer completions + external lease count zero
                -> generation increment -> pool recyclable
```

`zero_copy`は同一native allocationを共有する場合、`shared_surface`は同一surfaceをAPI間で共有する場合、`gpu_copy`はGPU内の別allocationへcopyする場合とする。いずれも`cpu_readback`を含まない。外部library内部のcopy有無は、外部consumerが申告可能な場合を除き`unknown_external`として境界traceへ記録する。

### 9.7 CPU Frame、Lease、Interop

- `INT-CPUINT-001`: CPU export descriptorはplane pointer、length、shape、stride、format、PTS、access modeとnative owner leaseを保持する。
- `INT-CPUINT-002`: borrowed NumPy viewはbase/capsule経由でnative ownerをretainし、最後のview解放までdecode slotをpoolへ戻さない。既定はread-onlyとする。
- `INT-CPUINT-003`: 同期borrowed encodeは呼出中だけpointerを参照し、codecがinputを読了してからreturnする。
- `INT-CPUINT-004`: 非同期borrowed encodeはsubmission objectがPython/.NET/native ownerをcompletionまで保持し、完了・cancel・failure時に一度だけ解放する。
- `INT-CPUINT-005`: .NETおよび高throughput非同期経路は長時間managed pinningを避け、固定容量native/pinned pool、backpressure、generation検査を使う。
- `INT-CPUINT-006`: import時にplane count、dtype、shape、stride、alignment、format、writabilityを検証し、strict時は共有不能layoutを拒否し、copy許可時だけ明示copyする。
- `INT-CPUINT-007`: GPU frameからstandard NumPyへの変換は必ず`cpu_readback`を記録し、BGR等への形式変換allocationも別edgeとして記録する。
- `INT-CPUINT-008`: native finalizer/release callbackはinterpreter shutdown後にPython APIを呼ばず、exceptionを越境させない。

## 10. Performance / Observability

Status: `PROPOSED`

- `INT-OBS-001`: demux、decode、surface wait、queue wait、export、import dependency wait、upload/download、CPU convert、encode、muxを別metricにする。
- `INT-OBS-002`: CPU timerとGPU event timerを区別する。
- `INT-OBS-003`: input/encoded fps、drop、peak queue、prefetch hit/miss、RAM/VRAM概算を公開する。
- `INT-OBS-004`: copy path判定は実際に実行したoperationから設定し、requested pathから推測しない。
- `INT-PERF-001`: benchmarkは1080p30/60、4K30、対応時4K60をbackend別に保存する。
- `INT-PERF-002`: absolute target確定前は直近承認baselineに対する回帰でgateする。
- `INT-PERF-003`: long-runでresource countが単調増加しないことをtraceする。

## 11. Security

Status: `PROPOSED`

- `INT-SEC-001`: container size、track count、packet size、resolution、frame countに上限とoverflow checkを設ける。
- `INT-SEC-002`: path、codec metadata、backend optionをcode/shellとして評価しない。
- `INT-SEC-003`: untrusted bitstreamによるlibrary errorをC ABI errorへ閉じ込める。
- `INT-SEC-004`: dynamic library searchはOSの安全な検索規則と検証済みlibrary名を使用する。
- `INT-SEC-005`: fuzz test対象をdemux、C ABI struct validation、packet/frame validationとする。
- `INT-SEC-006`: dependency SBOMとsecurity update手順を維持する。

## 12. C ABI / Binding Rules

Status: `PROPOSED`

- `INT-ABI-001`: symbol prefixは`mkvc_`、calling conventionをplatformごとに固定する。
- `INT-ABI-002`: ABIには`std::string/vector/bool/long`を露出しない。
- `INT-ABI-003`: allocatorを越境させず、Core生成物はCoreのrelease APIで解放する。
- `INT-ABI-004`: compatible minor versionでは既存struct prefixを保持する。
- `INT-PY-001`: pybind11 wrapperはNumPy dtype/shape/strideを検証し、native errorをPython exceptionへ変換する。
- `INT-CS-001`: P/Invoke struct layoutを自動testし、handleをSafeHandleで所有する。

## 13. Test Requirements

Status: `PROPOSED`

`docs/test-spec/test-requirements.md`の全`TEST-*`を実装する。各INT ruleに少なくとも1 testを割り当てる。

## 14. Design Decisions / Alternatives

Status: `CONFIRMED`

- `ADR-ARCH-001`: C++ class直接公開ではなくC ABIを採用する。理由はPython/C#/compiler間のABI安定性。
- `ADR-CODEC-001`: H.264/HEVCを除外しVP9/AV1に限定する。
- `ADR-CPU-001`: CPU AV1をSVT-AV1 encode/libaom decodeへ分割する。
- `ADR-NV-001`: NVIDIA SDK sampleを使わずnv-codec-headers + dynamic driver loadを採用する。
- `ADR-NP-001`: 安全なowned NumPy APIを既定として維持し、明示的なborrowed APIをowner lease/completion付きで追加する。
- `ADR-DROP-001`: file完全性のためMVPはblock/try_writeのみとする。
- `ADR-PKG-001`: OSS CPU dependencyをartifactへ同梱しvendor driver/runtimeは同梱しない。

## 15. Known Limitations

Status: `CONFIRMED`

- NVIDIA NVENCでVP9 encodeできない。
- zero-copyはdevice/context/format/operation互換時のみ成立する。
- NumPy入力はCPU memoryであり、GPU backendではuploadが必要。
- standard NumPy出力もCPU memoryであり、GPU decodeから取得するとdownloadが必要。GPU zero-copyにはDLPackまたはnative handleを使う。
- 外部GPU library内部のcopy/processingは本libraryの観測・保証範囲外である。
- process強制終了時のcontainer finalizeを保証しない。
- backend間の同一quality値は同一画質を保証しない。
- MVPは映像1track、固定FPS中心、Python 3.12、x64のみ。

## 16. Open Questions

Status: `PROPOSED`

- hardware class別の絶対性能SLO。
- project licenseと公開brand名。
- package容量によるCPU AV1 optional splitの要否。

## 17. Traceability

`docs/traceability.md`と`docs/design-model.json`を正とする。
