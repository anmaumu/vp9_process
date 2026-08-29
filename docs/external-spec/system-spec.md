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

Pythonおよび将来のC#から、OpenCVに近い操作感でMatroska/WebM映像をdecode/encodeする。CPU、Intel GPU、NVIDIA GPUを共通APIから選択でき、対応環境ではGPU Surfaceとzero-copy経路を利用できる。

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

### 2.2 Out of Scope

- macOS、VideoToolbox、Metal
- H.264、HEVC、およびこれらへのfallback
- 初期releaseでの音声、字幕、複数映像track、chapter、attachment
- 初期releaseでのlive配信、frame drop、borrowed NumPy
- ARM64
- 全環境でのzero-copy保証

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
| `UC-ABI-001` | C/C#利用者 | C ABI handleを介して同じCoreを使用する |
| `UC-DIAG-001` | 運用・開発者 | device、capability、実際のcopy path、性能統計を確認する |

## 4. 用語

Status: `CONFIRMED`

- **backend**: libvpx、SVT-AV1、libaom、oneVPL、NVDEC、NVENC等の実装。
- **device**: backendが利用するCPUまたはGPU adapter。
- **CPU frame**: host memory上のBGR/RGB/BGRA/I420/NV12 plane。
- **GPU Surface**: D3D11 Texture、VA-API Surface、CUDA device memory/array。
- **lease**: consumer完了までSurface再利用を禁止する寿命契約。
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

### 5.3 Encode

- `EXT-ENC-001`: `VideoWriter`は`write`、context manager、idempotentな`flush/close/release`を提供する。
- `EXT-ENC-002`: BGR/RGB/BGRA、I420、NV12 CPU入力を受け付ける。
- `EXT-ENC-003`: `write_surface`で互換GPU Surfaceを受け付ける。
- `EXT-ENC-004`: `write_batch`を提供する。
- `EXT-ENC-005`: 初期releaseのNumPy入力はlibrary-owned bufferへ安全にcopyする。
- `EXT-ENC-006`: queue満杯時の既定動作はblockとし、`try_write`は`False/WOULD_BLOCK`を返す。

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

### 5.6 Mode / Rate control

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
mkvc_get_last_error();
```

### 6.4 C#

- `EXT-CS-002`: handleは`SafeHandle`、reader/writer/frameは`IDisposable`で包む。
- `EXT-CS-003`: CPU frame API後にD3D11 Texture連携を提供する。
- `EXT-CS-004`: C++ Coreのsubmit/receiveを基に、将来`WriteAsync`等を提供できる設計とする。

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
| `AC-SYS-001` | Windows x64/Linux x64でCPU Coreをbuild/loadできる | EXT-SYS-001, EXT-BACK-006 |
| `AC-CONT-001` | 生成MKV/WebMを独立toolで最後までdecodeでき、codec/解像度/fps/frame数/durationが一致する | EXT-CONT-001..003 |
| `AC-CODEC-001` | VP9 CPU round-tripが成立する | EXT-CODEC-001 |
| `AC-CODEC-002` | AV1の各対応backendでround-tripが成立する | EXT-CODEC-002 |
| `AC-CODEC-003` | H.264/HEVCが列挙・受理・fallbackされない | EXT-CODEC-003 |
| `AC-DEC-001` | read各形式、iterator、batch、EOS、prefetchが契約通り動作する | EXT-DEC-001..005 |
| `AC-ENC-001` | 各CPU入力、batch、flush、close、try_writeが契約通り動作する | EXT-ENC-001..010 |
| `AC-BACK-001` | device/capability/auto selectionが実機能力と一致する | EXT-BACK-001..006 |
| `AC-FRAME-001` | Surface lease中の再利用がなく、release後accessを拒否する | EXT-FRAME-001..005 |
| `AC-ZC-001` | zero-copy対応経路をtraceで証明し、require時に降格しない | EXT-FRAME-003..004 |
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


