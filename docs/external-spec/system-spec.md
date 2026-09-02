---
document_type: external-specification
document_id: EXT-SYSTEM
status: proposed
profile: external-spec@1.1
source_snapshot: ../../SPECIFICATION.md
---

# MKVCodec 外部仕様書

> 本文書は利用者・外部システムから観測可能な振る舞いを定義する。実装方法は内部仕様書で扱う。

## 1. 目的・背景

Status: `CONFIRMED`

Python/C/C++/C#からMatroska/WebM映像をdecode/encodeし、CPU、Intel GPU、NVIDIA GPU上のframeを外部画像処理libraryへ安全にexportし、その処理結果をencodeへimportする。Coreの責務はcodec、container、memory interop、所有権、同期、copy-path検証であり、画像処理algorithmそのものではない。

Sources:

- `human_decision`
- `SPECIFICATION.md#1-目的`

## 2. Scope / Out of Scope

Status: `CONFIRMED`

### 2.1 Scope

- `EXT-SYS-001`: Windows x64とLinux x64を対象とする。
- `EXT-CONT-001`: `.mkv`と`.webm`の映像1trackをread/writeできる。
- `EXT-CODEC-001`: VP9をdecode/encodeできる。
- `EXT-CODEC-002`: AV1をdecode/encodeできる。
- `EXT-PY-001`: Python 3.12向けAPIを提供する。
- `EXT-ABI-001`: Python/C#から共有可能なC ABIを提供する。
- `EXT-CS-001`: C# P/Invokeを初期からsmoke testし、後続phaseで高水準APIを提供する。
- `EXT-BACK-001`: CPU、Intel GPU、NVIDIA GPUを実行時に列挙・選択できる。
- `EXT-SYS-002`: decode frameのCPU/GPU zero-copy exportと、外部処理結果のCPU/GPU importを共通のlease/completion契約で提供する。

### 2.2 Out of Scope

- macOS、VideoToolbox、Metal
- H.264、HEVC、およびこれらへのfallback
- 初期releaseでの音声、字幕、複数映像track、chapter、attachment
- 初期releaseでのlive配信、frame drop
- ARM64
- 全環境でのzero-copy保証
- GPU resize/crop/rotate/flip/letterbox、NPP kernel、oneVPL VPP、OpenCL/SYCL kernel、AI推論などの画像処理algorithm

Sources:

- `human_decision`
- `SPECIFICATION.md#2-スコープ`
- `SPECIFICATION.md#16-将来検討事項`

## 3. Actor / Use Case

Status: `CONFIRMED`

| ID | Actor | Use Case |
|---|---|---|
| `UC-DEC-001` | Python利用者 | MKV/WebMを開き、BGR/I420/NV12またはGPU Surfaceを順次取得する |
| `UC-ENC-001` | Python利用者 | NumPy/OpenCV frameまたはGPU SurfaceをVP9/AV1のMKV/WebMへ書く |
| `UC-TRANS-001` | Native/GPU利用者 | decode SurfaceをCPUへ戻さずencodeへ渡す |
| `UC-INTEROP-001` | 外部画像処理利用者 | decode frameを外部libraryへexportし、処理済みframeをcopyなしでencodeへimportする |
| `UC-ABI-001` | C/C#利用者 | C ABI handleを介して同じCoreを使用する |
| `UC-DIAG-001` | 運用・開発者 | device、capability、実際のcopy path、性能統計を確認する |

## 4. 用語

Status: `CONFIRMED`

- **backend**: libvpx、SVT-AV1、libaom、oneVPL、NVDEC、NVENC等の実装。
- **device**: backendが利用するCPUまたはGPU adapter。
- **CPU frame**: host memory上のBGR/RGB/BGRA/I420/NV12 plane。
- **GPU Surface**: D3D11 Texture、VA-API Surface、CUDA device memory/array。
- **lease**: consumer完了までSurface再利用を禁止する寿命契約。
- **borrowed frame**: ownerのmemoryを複製せず参照し、lease中だけ有効なframe。
- **imported frame**: 外部ownerのmemory/resourceをencode完了までleaseして利用するframe。
- **completion**: producerまたはconsumerがresource利用を完了したことを示すtoken/event/fence。
- **zero-copy**: 映像pixelのCPU round-tripおよびGPU内中間copyを行わない経路。
- **gpu-copy**: CPU round-tripはないがGPU内copy/VPPを伴う経路。
- **cpu-upload**: CPU frameをGPUへ転送する経路。
- **PTS**: Presentation timestamp。内部表現はsigned 64-bit nanoseconds。
- **prefetch**: decode済みframeをbounded queueへ先読みする件数。
- **async depth**: backendへ同時投入可能な処理数。

## 5. 機能要件

Status: `CONFIRMED`

### 5.1 Container / Codec

- `EXT-CONT-002`: 拡張子からDocTypeを決定し、`.webm`はWebM subset、`.mkv`はMatroskaとして検証する。
- `EXT-CONT-003`: 入力PTSがある場合は保持し、固定FPS生成時のみframe indexからPTSを生成する。
- `EXT-CODEC-003`: H.264/HEVCを公開、受理、暗黙選択しない。
- `EXT-CODEC-004`: backendとcodecの無効な組合せを初期化時に拒否する。

### 5.2 Decode

- `EXT-DEC-001`: `VideoCapture`は`read`、iterator、context manager、idempotentな`close/release`を提供する。
- `EXT-DEC-002`: `read_bgr`、`read_nv12`、`read_surface`を明示的に提供する。
- `EXT-DEC-003`: `read_batch(max_size, timeout_ms=0)`を提供する。
- `EXT-DEC-004`: `prefetch=0`の同期動作と、正数のbounded先読みを提供する。
- `EXT-DEC-005`: end-of-streamではPython APIの単発readは`None`、iteratorは`StopIteration`とする。
- `EXT-DEC-006`: CPU backendはnative decode planeをread-only NumPy viewとしてleaseする`read_borrowed`を提供し、owned `read`と明示的に区別する。

### 5.3 Encode

- `EXT-ENC-001`: `VideoWriter`は`write`、context manager、idempotentな`flush/close/release`を提供する。
- `EXT-ENC-002`: BGR/RGB/BGRA、I420、NV12 CPU入力を受け付ける。
- `EXT-ENC-003`: `write_surface`で互換GPU Surfaceを受け付ける。
- `EXT-ENC-004`: `write_batch`を提供する。
- `EXT-ENC-005`: 初期releaseのNumPy入力はlibrary-owned bufferへ安全にcopyする。
- `EXT-ENC-006`: queue満杯時の既定動作はblockとし、`try_write`は`False/WOULD_BLOCK`を返す。
- `EXT-ENC-011`: 同期`write_borrowed`は呼出中だけCPU inputを借用し、codecが読み終えてから戻る。
- `EXT-ENC-012`: 非同期`submit_borrowed`はsubmission completionまでinput ownerを保持し、変更禁止期間をAPI契約として公開する。
- `EXT-ENC-013`: 高throughput非同期入力向けに固定容量のnative/pinned buffer poolを提供し、未完了bufferの再取得を禁止する。

### 5.4 Backend / Device

- `EXT-BACK-002`: `list_devices`、`list_decoders`、`list_encoders`を提供する。
- `EXT-BACK-003`: capabilityにはbackend、device、codec、profile、bit depth、pixel format、最大解像度、handle、async対応、利用不可理由を含める。
- `EXT-BACK-004`: `backend="auto"`は指定codecを維持し、利用可能な候補を探索する。
- `EXT-BACK-005`: `device_preference`でvendor優先順を変更できる。
- `EXT-BACK-006`: GPU/driverがなくてもlibrary import/loadとCPU backend利用ができる。

既定探索順:

```text
VP9 encode: oneVPL -> libvpx -> error
AV1 encode: NVENC -> oneVPL -> SVT-AV1 -> error
VP9 decode: supported GPU backend -> libvpx -> error
AV1 decode: supported GPU backend -> libaom -> error
```

### 5.5 Memory / Copy path

- `EXT-FRAME-001`: CPU frameはplane、stride、width、height、format、PTSを保持する。
- `EXT-FRAME-002`: GPU SurfaceはD3D11、VA-API、CUDAを識別可能にする。
- `EXT-FRAME-003`: `require_zero_copy=True`でzero-copy不能ならcopyへ降格せず失敗する。
- `EXT-FRAME-004`: 実際に選択した`cpu/cpu_upload/gpu_copy/zero_copy`を統計で公開する。
- `EXT-FRAME-005`: released Surfaceへのaccessは判定可能なerrorとする。

### 5.5.1 CPU Frame Interoperability

- `EXT-FRAME-006`: CPU exportはplane pointer、length、shape、stride、format、PTS、read-only/writable属性とowner leaseを公開する。
- `EXT-FRAME-007`: borrowed NumPy viewは対応するnative allocationを共有し、viewまたは明示leaseが残る間はbufferをpoolへ戻さない。
- `EXT-FRAME-008`: standard NumPyはCPU memoryとして扱い、GPU decodeからNumPyを要求した場合は`gpu_download`と必要な形式変換をcopy-pathへ記録する。
- `EXT-FRAME-009`: CPU importはI420/NV12および対応するpacked形式のpointer/shape/strideを検証し、同期borrowまたはcompletion付き非同期leaseとしてencoderへ渡す。
- `EXT-FRAME-010`: BGR/RGB等からcodec入力形式への変換はzero-copyとは報告しない。同一format/layoutを共有する場合だけCPU zero-copyとする。
- `EXT-FRAME-011`: .NET同期borrowは呼出中だけmanaged arrayをpinし、非同期経路は長時間pinningを避けるnative/pinned poolを推奨・提供する。
- `EXT-FRAME-012`: 外部CPU libraryが連続配列、alignmentまたは特定strideを要求して共有不能な場合、strict指定では失敗し、copy許可時だけ明示copyする。

### 5.5.2 GPU-resident Frame Interoperability

- `EXT-GPU-001`: 共通opaque handle `mkvc_gpu_frame`はbackend、device identity、memory type、pixel format、dimensions、planes/pitch、color metadata、PTSを問い合わせ可能にする。
- `EXT-GPU-002`: `mkvc_gpu_frame`はretain/release可能なleaseとし、producer completionと全consumer lease解放の両方が成立するまでnative resourceを再利用・破棄しない。
- `EXT-GPU-003`: completionはquery/wait(timeout)/dependency登録を提供し、通常経路でdevice-wide synchronizationを要求しない。
- `EXT-GPU-004`: IntelではoneVPL decode surfaceをD3D11/VA-API/対応時USMとして外部libraryへexportし、外部処理済みresourceをoneVPL encodeへimportする。D3D11/VA importはmemory interface 1.0と互換minor revisionの`ImportFrameSurface`を使い、未公開function、未知major ABI、copy-only runtimeでは`NOT_SUPPORTED`を返す。最初の外部入力で同じdevice/displayのvideo-memory encoderへ紐づけ、既存CPU/direct sequenceからの切替やdevice/display変更にはflushを要求する。borrowed device保護のため最初の外部frameをflush/closeまで保持するので、利用者はその分のpool容量を確保する。
- `EXT-GPU-005`: NVIDIAではNVDEC outputをCUDA/DLPackとして外部libraryへexportし、外部処理済みCUDA resourceをNVENCへimportする。
- `EXT-GPU-006`: Intel GPU frameからWindows D3D11 texture/subresourceおよびLinux VA display/surfaceのborrowed native handleを取得できる。D3D11/VA resourceの所有権はlibraryに残す。
  外部importの元ownerは、出力SyncPoint完了後もruntime入力参照がある間保持する。参照中のimport wrapperの上限は64とし、上限でwriteはWOULD_BLOCKを返す。呼出側はflushでdrainしてから再試行する。最初のdevice anchorの保持と合わせてpool容量を見積もる。
- `EXT-GPU-007`: NVIDIA GPU frameからCUDA device pointerまたはCUarray、pitch、CUDA context/device、producer stream/eventをborrowed viewとして取得できる。
- `EXT-GPU-008`: PythonではIntel USM対応経路およびNVIDIA CUDA対応経路をDLPack protocolで受け渡しでき、consumer指定streamへ正しいdependencyを設定する。
- `EXT-GPU-009`: CUDA pointer/CUarray、D3D11 texture、VA surface、対応時USM/DLPackのimport APIはresource owner、layout、device/context、producer completion、release callbackを受け取る。
  Windows D3D11 NV12の`mkvc_gpu_frame_import_d3d11_fence`はhandles=(texture, subresource=0, fence, target)を受け取り、targetは1..UINT64_MAX-1、queryはnull必須とする。同一device、GPU-only、single-subresource、寸法一致を検査しCOM参照を保持する。producerは処理後のSignalとcommand dispatchを完了させ、fenceの巻戻し/target再利用やconsumer完了前の書込みを禁止する。library側ではfence値のみpollし、Flush/Map/copy/device-wide waitを行わない。非WindowsはNOT_SUPPORTED、descriptor不正はINVALID_ARGUMENT、device removalはCODECとし、失敗時はownerを受け取らない。C++/Python/.NETに同等入口を設ける。oneVPL encoderの対応可否は同期の対応可否と独立に判定する。
  Linux Intel NV12 VA surfaceは`mkvc_gpu_frame_import_va_surface`でVAに投入済みのproducer処理をnative同期できる。query callbackはnull必須。C++/Python/.NETにも同等入口を公開する。surface ID 0は有効、`UINT32_MAX`は無効とし、ownerはdisplayとsurfaceの両方を最終leaseまで保持する。未対応platform/build、libva symbol不足、driver未実装は`NOT_SUPPORTED`で失敗し、失敗時にowner/release callbackの所有権を受け取らない。import後の追加書込みは禁止。VA同期はOpenCL/SYCL等の独立した処理を保証せず、汎用producer queryまたは明示的な外部同期を必要とする。Pythonの`producer_synchronized=True`は利用者がその同期を完了した場合だけ許可する。
- `EXT-GPU-010`: `require_gpu_resident=True`、`allow_gpu_copy`、`allow_cpu_copy`をdecode/export/import/encode全体へ適用し、edge別copy-pathとfallback理由を返す。

C/C++/C#利用者はversioned `mkvc_copy_policy`を作成直後に
`mkvc_encoder_set_copy_policy` / `mkvc_decoder_set_copy_policy`へ渡す。
policyは最初のframe operation後には変更できない。初期実装のstrict Intel
surface経路はencoder `queue_size=0`、decoder `prefetch=0`を要求する。

Native handleはborrowed exportであり、利用者は元の`mkvc_gpu_frame` leaseを保持する。
DLPack exportは`DLManagedTensor` deleterが独立したnative leaseを保持するため、consumerへ
所有権を渡した後は元のPython/C handleを先に解放できる。未消費capsuleのdestructorと
consumer側deleterのどちらか一方だけがこのleaseを解放する。

### 5.6 CPU Convenience Processing

- `EXT-PROC-001`: 既存のCPU owned frame向け`process/resize/crop/convert/rotate/flip`は任意の便利機能として提供する。
- `EXT-PROC-002`: `resize(width, height, fit, interpolation)`を提供し、`stretch/contain/cover`とbackendが対応する補間方式を選択できる。
- `EXT-PROC-003`: `crop(x, y, width, height)`を提供し、chroma subsampling、境界、偶数alignmentを検証する。
- `EXT-PROC-004`: NV12/P010/I420/BGR/RGB/BGRA間の基本色変換と、BT.601/BT.709/BT.2020、limited/full rangeのmetadata保持・変換を提供する。
- `EXT-PROC-005`: 90/180/270度rotateとhorizontal/vertical flipを提供する。
- `EXT-PROC-006`: letterbox/pillarboxを提供し、出力寸法、縦横比、配置、背景色を明示指定できる。
- `EXT-PROC-007`: GPU frameに対する画像処理methodは本libraryのscope外とし、native handle/DLPack exportを案内する。
- `EXT-PROC-008`: CPU便利処理の結果はowned frameとし、形式変換・allocationをzero-copyと報告しない。

CPU convenience API:

```python
processed = frame.process(
    crop=(0, 0, 1920, 1080),
    resize=(1280, 720),
    fit="contain",
    format="nv12",
    color_space="bt709",
    color_range="limited",
    rotate=0,
    flip=None,
    background=(0, 0, 0),
)
writer.write(processed)
```

個別の`resize/crop/convert/rotate/flip`は同じCPU処理planを生成する便宜APIとする。GPU利用者はCuPy/NPP/VA-API/OpenCL/SYCL/D3D11/DirectML等で処理し、処理結果をimport APIへ渡す。

### 5.7 Mode / Rate control

- `EXT-PERF-001`: `low_latency`、`balanced`、`throughput` modeを提供する。
- `EXT-ENC-007`: 既定rate controlは`quality`、既定`quality=32`（0最高、63最低）とする。
- `EXT-ENC-008`: `vbr`では`bitrate_kbps`を必須、`cbr`は対応backendのみ許可する。
- `EXT-ENC-009`: 既定keyframe intervalは4秒、threadsはautoとする。
- `EXT-ENC-010`: backend固有詳細は`backend_options`へ隔離する。

balanced初期値:

| Backend | 初期値 |
|---|---|
| libvpx VP9 | CQ、cq-level=quality、cpu-used=6、row-mt |
| SVT-AV1 | CRF=quality、preset 8 |
| oneVPL | 利用可能なquality-based RC、balanced相当TargetUsage |
| NVENC AV1 | P4、HQ、VBR/CQ相当、capability依存lookahead |

## 6. External Interfaces

Status: `PROPOSED`

### 6.1 Python decode

```python
with mkvcodec.VideoCapture(
    "input.mkv",
    backend="auto",
    device="auto",
    output_memory="cpu",
    prefetch=4,
    mode="balanced",
) as cap:
    frame = cap.read_bgr()

# allocationを共有するread-only view。viewを保持している間はdecode slotを再利用しない。
with cap.read_borrowed(format="nv12") as frame:
    y_plane = frame.planes[0]
```

### 6.2 Python encode

```python
with mkvcodec.VideoWriter(
    "output.mkv",
    codec="vp9",
    backend="auto",
    fps=30,
    frame_size=(1920, 1080),
    quality=32,
    async_depth=4,
    queue_size=8,
) as writer:
    writer.write(bgr_frame)

# 同一layoutのCPU memoryを呼出中だけ借用する同期経路
writer.write_borrowed(nv12_frame)

# 非同期経路ではcompletionまでinputを変更・解放しない
submission = writer.submit_borrowed(pool_buffer)
submission.wait()
```

GPU external processing:

```python
with cap.read_surface() as decoded:
    # NVIDIAではCuPy等がDLPackをconsumeする。Intel texture/VA surfaceは
    # 対応するnative API経由で外部libraryへ渡す。
    external = process_with_external_library(decoded)
    writer.write_surface(external, require_gpu_resident=True)
```

### 6.3 C ABI

- `EXT-ABI-002`: symbol prefixは`mkvc_`とする。
- `EXT-ABI-003`: opaque handle、固定幅整数、C互換struct、error codeを使用する。
- `EXT-ABI-004`: `create/destroy`、`retain/release`、`flush`、`get_last_error`を提供する。
- `EXT-ABI-005`: 公開structは`struct_size`とversionを持つ。

主要symbol:

```c
mkvc_decoder_create();
mkvc_decoder_read();
mkvc_decoder_destroy();
mkvc_encoder_create();
mkvc_encoder_write_frame();
mkvc_encoder_flush();
mkvc_encoder_destroy();
mkvc_frame_retain();
mkvc_frame_release();
mkvc_frame_get_view();
mkvc_encoder_write_frame_borrowed();
mkvc_encoder_submit_frame_borrowed();
mkvc_submission_query();
mkvc_submission_wait();
mkvc_submission_release();
mkvc_cpu_frame_pool_create();
mkvc_cpu_frame_pool_acquire();
mkvc_cpu_buffer_get_desc();
mkvc_cpu_buffer_get_view();
mkvc_encoder_submit_cpu_buffer();
mkvc_cpu_buffer_release();
mkvc_encoder_cancel();
mkvc_gpu_frame_get_native_handle();
mkvc_gpu_frame_import_external();
mkvc_gpu_frame_import_cuda_event();
mkvc_gpu_frame_import_va_surface();
mkvc_gpu_frame_import_d3d11_fence();
mkvc_encoder_write_gpu_frame();
mkvc_gpu_frame_query_completion();
mkvc_gpu_frame_wait();
mkvc_get_last_error();
```

export descriptorはmemoryを所有せず、対応するframe leaseがdescriptorの有効期間を支配する。import submissionはconsumer completionまで外部ownerをretainし、その後release callbackを一度だけ呼ぶ。
NVIDIA CUDA pointerの非同期importでは`native_handle.handles[1]`を`CUcontext`、
`handles[3]`をproducerが記録した`CUevent`として渡す。event/contextは最終lease release
まで有効でなければならず、libraryはdevice-wide synchronizeを行わずeventをpollする。
Pythonの連続NV12 DLPack importはCUDA `uint8[height*3/2,width]`だけを受理し、row strideを
pitchとして使用する。DLPackに含まれないCUDA contextとproducer eventは別途明示する。
CUDA array importは1 channel uint8、width列、height*3/2行の2D arrayを契約とし、
descriptor pitchはwidthと一致させる。実allocation形状の最終検証はCUDA/NVENC driverが行う。

### 6.4 C#

- `EXT-CS-002`: handleは`SafeHandle`、reader/writer/frameは`IDisposable`で包む。
- `EXT-CS-003`: CPU frame API後にD3D11 Texture連携を提供する。
- `EXT-CS-004`: C++ Coreのsubmit/receiveを基に、将来`WriteAsync`等を提供できる設計とする。

.NETの同期CPU入力はP/Invoke中だけmanaged memoryをpinできる。`WriteAsync`はmanaged arrayを長時間pinせず、libraryのnative/pinned poolまたは明示的なunmanaged ownerを使用する。

## 7. Error Behavior

Status: `CONFIRMED`

- `EXT-ERR-001`: unsupported codec/backend/device/formatは初期化または投入時に明示errorとする。
- `EXT-ERR-002`: C++ exceptionをC ABI外へ出さない。
- `EXT-ERR-003`: C ABIはstable error codeとthread-local detail stringを返す。
- `EXT-ERR-004`: Python/C# bindingはC errorを型付きexceptionへ変換する。
- `EXT-ERR-005`: error/cancel/close時も可能な限りflush/finalize/cleanupする。
- `EXT-ERR-006`: process強制終了時の完全なMKV finalizeは保証しない。

## 8. Performance / Non-functional Requirements

Status: `PROPOSED`

- `EXT-PERF-002`: queue、Surface、bitstream poolは設定上限を超えて成長しない。
- `EXT-PERF-003`: balanced modeは複数frameをpipelineし、毎frameの全GPU同期を通常経路で行わない。
- `EXT-PERF-004`: zero-copy報告時、映像pixelのCPU round-tripがない。
- `EXT-PERF-005`: Pythonの長時間処理・待機中はGILを解放する。
- `EXT-PERF-006`: 1080p30/60、4K30、対応時4K60のbackend別baselineを記録し、理由のない重大回帰をrelease gateとする。
- `EXT-OBS-001`: queue wait、upload、convert、codec、mux、fps、drop、peak queue、copy pathを観測できる。

絶対fps目標は対象hardwareのbaseline採取後に確定する。それまでは性能数値に関するStatusを`PROPOSED`とする。

## 9. Business Rules / Constraints

Status: `CONFIRMED`

- `EXT-PKG-001`: Windows/Linuxのwheel、NuGet、native artifactを対象とする。
- `EXT-PKG-002`: OSS CPU依存はnoticeを伴って同梱し、vendor GPU driver/runtimeは同梱しない。
- `EXT-PKG-003`: NVIDIA adapterは`nv-codec-headers`を使うclean implementationとし、SDK sampleを取り込まない。
- `EXT-PKG-004`: artifactにLICENSE、PATENTS、THIRD_PARTY_NOTICES、SBOMを収録する。
- `EXT-PKG-005`: `MKVCodec`は作業名とし、商用公開前にMatroska名称利用確認またはneutral brandへの変更を行う。
- `EXT-PKG-006`: 詳細なGo/No-Goは`LICENSE_POLICY.md`に従う。

## 10. Acceptance Criteria

Status: `PROPOSED`

| ID | Acceptance criterion | Related EXT |
|---|---|---|
| `AC-SYS-001` | Windows x64/Linux x64でCPU Coreをbuild/loadでき、共通interop境界を列挙できる | EXT-SYS-001..002, EXT-BACK-006 |
| `AC-CONT-001` | 生成MKV/WebMを独立toolで最後までdecodeでき、codec/解像度/fps/frame数/durationが一致する | EXT-CONT-001..003 |
| `AC-CODEC-001` | VP9 CPU round-tripが成立する | EXT-CODEC-001 |
| `AC-CODEC-002` | AV1の各対応backendでround-tripが成立する | EXT-CODEC-002 |
| `AC-CODEC-003` | H.264/HEVCが列挙・受理・fallbackされない | EXT-CODEC-003 |
| `AC-DEC-001` | read各形式、iterator、batch、EOS、prefetch、borrowed readが契約通り動作する | EXT-DEC-001..006 |
| `AC-ENC-001` | 各CPU入力、borrowed/async submission、batch、flush、close、try_writeが契約通り動作する | EXT-ENC-001..013 |
| `AC-BACK-001` | device/capability/auto selectionが実機能力と一致する | EXT-BACK-001..006 |
| `AC-FRAME-001` | Surface lease中の再利用がなく、release後accessを拒否する | EXT-FRAME-001..005 |
| `AC-CPUINT-001` | CPU frameのborrowed export/importがpointer/shape/stride、lease、completion、strict copy policyを満たし、GPU→NumPy copyを明示する | EXT-FRAME-006..012 |
| `AC-ZC-001` | zero-copy対応経路をtraceで証明し、require時に降格しない | EXT-FRAME-003..004 |
| `AC-GPU-001` | Intel/NVIDIAでdecode→export→外部処理→import→encodeがGPU-resident契約、lease/completion、native/DLPack interop、copy policyを満たす。Linux VA native同期の部分受入れはpending/timeout/terminal failure、非対応時のfail-closed、native/Python実機encode、owner寿命を検証する。これだけでWindows fence、USM/DLPack、外部kernelや独立traceの全体受入れ完了とはしない | EXT-GPU-001..010 |
| `AC-PROC-001` | CPU owned frame向け5種の便利処理が幾何・色metadata契約どおり動作し、GPU処理はinteropへ案内される | EXT-PROC-001..008 |
| `AC-ABI-001` | C/C#/Pythonから同じCoreのcreate/read-write/destroyが成立する | EXT-ABI-001..005, EXT-CS-001..004 |
| `AC-ERR-001` | 全失敗でexception leak、double free、resource leakがない | EXT-ERR-001..006 |
| `AC-PERF-001` | bounded resource、pipeline並行性、GIL解放、baseline回帰gateを満たす | EXT-PERF-001..006 |
| `AC-OBS-001` | backend別時間内訳とcopy pathを取得できる | EXT-OBS-001 |
| `AC-PKG-001` | artifact内容検査とlicense gateがPASSする | EXT-PKG-001..006 |

詳細なtestとの対応は`../traceability.md`を正とする。

## 11. Open Questions

Status: `PROPOSED`

- `TBD-PERF-001`: hardware class別の絶対fps/latency目標。
- `TBD-LIC-001`: project自身をApache-2.0で公開するか。
- `TBD-BRAND-001`: 商用公開brand名。
- `TBD-PKG-001`: CPU AV1依存を含むwheel容量が単一package許容範囲か。

## 12. Traceability

完全なtraceabilityは`../traceability.md`と`../design-model.json`を正とする。
