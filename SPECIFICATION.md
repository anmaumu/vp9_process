# MKVCodec 仕様書

- ステータス: Draft 0.1
- 対象OS: Windows x64 / Linux x64
- 対象言語: Python（初期）、C#（将来。ただしC ABIスモークテストは初期から実施）
- 対象コンテナ: Matroska（`.mkv`）/ WebM（`.webm`）
- 対象映像コーデック: VP9 / AV1

## 1. 目的

MKV/WebM映像を、PythonおよびC#からOpenCVに近い操作感でデコード・エンコードするライブラリを提供する。

CPU、Intel GPU、NVIDIA GPUを共通APIから選択可能にし、CPUフレームだけでなくGPU Surfaceを直接受け渡せる設計とする。利用可能な場合はGPU内コピーまたはゼロコピーを使用し、対応不能な場合は理由と実際に選択した経路を利用者へ公開する。

## 2. スコープ

### 2.1 対象

- Windows x64およびLinux x64
- 映像のみのMKV/WebM読み書き
- VP9およびAV1
- CPU、Intel GPU、NVIDIA GPUバックエンド
- Python API
- 安定したC ABI
- 将来のC# P/Invoke API
- 同期APIと内部非同期パイプライン
- 固定FPS
- 入力由来PTSの保持
- CPUフレームおよびGPU Surface
- 実行時capability検出
- バックプレッシャー、Surface lease、性能統計

### 2.2 非対象

- macOS、VideoToolbox、Metal
- H.264およびHEVC
- H.264/HEVCへの暗黙フォールバック
- 初期リリースでの音声、字幕、複数映像トラック
- 初期リリースでの可変フレームレート編集
- 初期リリースでのネットワーク配信
- 全GPU・全ドライバーでのゼロコピー保証

## 3. 基本方針

1. C++ Coreを処理本体とし、言語バインディングを薄く保つ。
2. DLL/共有ライブラリの公開境界はC ABIとする。
3. demux/mux、codec、メモリ表現、OS依存GPU連携を分離する。
4. バックエンドとcodecを独立して選択・検証する。
5. 指定codecを暗黙に変更しない。
6. 同期的で使いやすい外部APIを提供し、内部は非同期処理可能にする。
7. キューは必ず上限付きとし、無制限なメモリ増加を許さない。
8. GPU Surfaceの所有権と完了同期を明示的に管理する。
9. 性能経路を `zero_copy`、`gpu_copy`、`cpu_upload`、`cpu` として可視化する。
10. GPUバックエンドが利用できない環境でも、ライブラリ本体をロードできるようにする。

## 4. 対応マトリクス

### 4.1 エンコード

| Codec | CPU | Intel GPU | NVIDIA GPU |
|---|---|---|---|
| VP9 | libvpx | oneVPL（実機capability依存） | 非対応 |
| AV1 | SVT-AV1 | oneVPL（実機capability依存） | NVENC（実機capability依存） |

### 4.2 デコード

| Codec | CPU | Intel GPU | NVIDIA GPU |
|---|---|---|---|
| VP9 | libvpx | oneVPL | NVDEC |
| AV1 | libaom | oneVPL | NVDEC |

表は設計上の対象を示す。実際の可否はGPU世代、ドライバー、SDK、解像度、bit depth、pixel formatなどを実行時に問い合わせて決定する。

## 5. システム構成

```text
Python (pybind11) ----+
                      +--> C ABI --> C++ Core
C# (P/Invoke) --------+                |
                                       +-- IDemuxer / IMuxer
                                       +-- IVideoDecoder
                                       +-- IVideoEncoder
                                       +-- PixelConverter / VPP
                                       +-- Device & Capability Registry
                                       +-- Queue / Surface Pool / Statistics
```

### 5.1 モジュール案

```text
mkvcodec/
|-- include/mkvcodec/
|   |-- mkvcodec.h
|   `-- version.h
|-- src/
|   |-- core/
|   |-- c_api/
|   |-- containers/libwebm/
|   `-- backends/
|       |-- libvpx/
|       |-- onevpl/
|       `-- nvidia/
|-- bindings/
|   |-- python/
|   `-- dotnet/
`-- tests/
    |-- unit/
    |-- integration/
    `-- hardware/
```

## 6. 外部仕様

### 6.1 PythonデコードAPI

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

公開操作:

- `read()` : 設定された既定形式で1フレーム取得。終端は `None`。
- `read_bgr()` : CPU BGR `numpy.ndarray`を取得。
- `read_nv12()` : CPU NV12フレームを取得。
- `read_surface()` : GPU Surface leaseを取得。
- `read_batch(max_size, timeout_ms=0)` : 最大件数まで一括取得。
- `close()` / `release()` : 処理停止とリソース解放。複数回呼び出し可能。
- iterator / context manager対応。
- `stats` : デコード統計。
- `info` : codec、解像度、時間情報、選択backendなど。

### 6.2 PythonエンコードAPI

```python
with mkvcodec.VideoWriter(
    "output.mkv",
    codec="vp9",
    backend="auto",
    device="auto",
    fps=30,
    frame_size=(1920, 1080),
    quality=32,
    mode="balanced",
    async_depth=4,
    queue_size=8,
) as writer:
    writer.write(bgr_frame)
```

公開操作:

- `write(frame)` : CPU NumPy/OpenCVフレームを書き込む。
- `write_i420(frame)` : CPU I420入力。
- `write_nv12(y, uv)` : CPU NV12入力。
- `write_surface(surface)` : GPU Surface入力。
- `write_batch(frames)` : 複数フレーム投入。
- `flush()` : 遅延中のパケットを出力する。
- `close()` / `release()` : flush、mux finalize、解放。複数回呼び出し可能。
- context manager対応。
- `stats` : エンコード統計。

### 6.3 共通設定

- `backend`: `auto`, `libvpx`, `onevpl`, `nvdec`, `nvenc`
- `device`: `auto`または列挙結果のdevice ID
- `codec`: `vp9`, `av1`
- `mode`: `low_latency`, `balanced`, `throughput`
- `overflow`: 初期リリースは `block` のみ。非blockingは `try_write()` を使用
- `require_zero_copy`: `True`の場合、コピーが必要なら初期化または投入時に失敗
- `prefetch`: デコード済みフレームの先読み上限
- `async_depth`: GPUへ同時投入可能な処理数
- `queue_size`: 未処理入力の最大件数

### 6.4 backend自動選択

`backend="auto"` は指定codecを維持して利用可能なbackendを探索する。

既定候補順:

```text
VP9 encode: oneVPL -> libvpx -> error
AV1 encode: NVENC -> oneVPL -> SVT-AV1 -> error
VP9 decode: 対応GPU backend -> libvpx -> error
AV1 decode: 対応GPU backend -> libaom -> error
```

候補順は `device_preference` で変更可能にする。H.264/HEVCへ変更してはならない。

### 6.5 capability API

```python
mkvcodec.list_devices()
mkvcodec.list_decoders()
mkvcodec.list_encoders()
```

列挙情報には最低限、次を含める。

- backend、device ID、device名、vendor
- decode/encode区分
- 対応codec、profile、bit depth
- 対応pixel format
- 最大解像度
- zero-copy対象handle
- async対応
- 利用不可の場合の理由

### 6.6 フレーム形式

CPU:

- BGR24、RGB24、BGRA32
- I420、NV12
- 将来P010
- planeごとのdata、stride、width、height、PTS

GPU:

- Windows: D3D11 Texture
- Linux: VA-API Surface
- Windows/Linux: CUDA device pointer / CUDA array

GPUフレームはleaseとして扱い、`release()`またはcontext manager終了まで有効とする。ただし非同期consumerが保持した場合、内部処理完了まで実リソースを再利用しない。

### 6.7 エラー方針

- codec/backend非対応は初期化時に明示的なエラーとする。
- 暗黙のcodec変更は禁止する。
- `require_zero_copy=True`でコピーが必要ならエラーとする。
- Pythonは型付き例外、C#は例外へ変換する。
- C ABIではエラーコードとthread-local詳細文字列を返す。
- `close()`中のエラーでも可能な限り全リソースを解放する。

## 7. C ABI仕様

C ABIは異なるコンパイラ・言語から利用可能な共通境界であり、内部実装はC++とする。

### 7.1 基本規約

- `extern "C"`で公開する。
- opaque handleを使用する。
- C++例外をABI外へ出さない。
- 固定幅整数とC互換構造体を使用する。
- 公開構造体には `struct_size` とversionを持たせる。
- DLL内で確保したリソースはDLLのrelease/destroy関数で解放する。
- create/destroy、retain/releaseを明確に対にする。
- ABI互換性を破る変更ではmajor versionを上げる。

### 7.2 主要関数案

```c
mkvc_result mkvc_decoder_create(
    const mkvc_decoder_config* config,
    mkvc_decoder_t** decoder);
mkvc_result mkvc_decoder_read(
    mkvc_decoder_t* decoder,
    mkvc_frame_t** frame);
void mkvc_decoder_destroy(mkvc_decoder_t* decoder);

mkvc_result mkvc_encoder_create(
    const mkvc_encoder_config* config,
    mkvc_encoder_t** encoder);
mkvc_result mkvc_encoder_write_frame(
    mkvc_encoder_t* encoder,
    const mkvc_frame_t* frame);
mkvc_result mkvc_encoder_flush(mkvc_encoder_t* encoder);
void mkvc_encoder_destroy(mkvc_encoder_t* encoder);

void mkvc_frame_retain(mkvc_frame_t* frame);
void mkvc_frame_release(mkvc_frame_t* frame);
size_t mkvc_get_last_error(char* buffer, size_t buffer_size);
```

### 7.3 C#方針

- P/InvokeでC ABIを呼ぶ。
- handleは `SafeHandle` でラップする。
- reader/writer/frameは `IDisposable` を実装する。
- CPUメモリは安全なcopy APIを初期実装とする。
- D3D11 Texture連携は所有権とdevice同一性を検証する。
- 初期段階からcreate/read-or-write/destroyのスモークテストを実施する。

## 8. 内部仕様

### 8.1 共通データモデル

```cpp
struct EncodedPacket {
  Buffer data;
  int64_t pts_ns;
  int64_t duration_ns;
  bool keyframe;
  Codec codec;
};

using VideoFrame = std::variant<
  CpuFrame,
  D3D11Frame,
  VaapiFrame,
  CudaFrame
>;
```

内部時間単位は符号付き64-bit nanosecondsへ統一する。入力PTSがある場合は保持し、固定FPS生成時のみframe indexからPTSを生成する。

### 8.2 共通インターフェース

```cpp
class IVideoDecoder {
 public:
  virtual void Submit(const EncodedPacket&) = 0;
  virtual std::optional<VideoFrame> TryReceive() = 0;
  virtual void Flush() = 0;
};

class IVideoEncoder {
 public:
  virtual void Submit(const VideoFrame&) = 0;
  virtual std::optional<EncodedPacket> TryReceive() = 0;
  virtual void Flush() = 0;
};
```

同期backendは専用workerでこのsubmit/receiveモデルへ適合させる。

### 8.3 コンテナ

- libwebmでdemux/muxする。
- VP9 trackはMatroska/WebMのVP9 CodecIDを使用する。
- AV1 trackは `V_AV1` と必要なcodec configurationを設定する。
- PTS、duration、keyframe情報をBlockへ反映する。
- `close()`で遅延パケット回収後にSegmentをfinalizeする。
- 初期リリースは映像1トラックのみとする。

### 8.4 CPUバックエンド

#### libvpx decode

```text
libwebm demux -> VP9 packet -> libvpx worker -> I420 queue -> consumer
```

- 1ストリームにつき原則1 decode worker。
- libvpx内部thread数を設定可能にする。
- bounded prefetch queueを使用する。
- BGR出力時はlibyuvで変換する。

#### libvpx encode

```text
CPU BGR -> libyuv I420 -> libvpx VP9 -> packet -> libwebm
```

- 1ストリームにつき原則1 encode worker。
- libvpx内部threadとアプリworkerの過剰並列を避ける。
- CPUバッファをプールして再利用する。
- `lag-in-frames`はmodeに応じて設定する。
- `close()`時にnull frame相当で遅延パケットを全回収する。

### 8.5 Intel oneVPLバックエンド

WindowsはD3D11、LinuxはVA-APIを使用する。

```text
packet -> DecodeFrameAsync -> GPU Surface
GPU Surface -> VPP(optional) -> EncodeFrameAsync -> packet
```

- SyncPointを複数保持し、毎フレーム直後の全面同期を避ける。
- `AsyncDepth`既定値はbalancedで4とする。
- CPU入力はGPU upload後、必要に応じてoneVPL VPPでNV12/P010へ変換する。
- decode/encodeで互換Surfaceを共有できる場合はzero-copyとする。
- 共有不能だが同GPUの場合はGPU copy/VPPを使用する。
- CPU planeアクセス時だけmap/copyする。
- 既存 `SurfaceFrame` のlease、prefetch、batch設計を共通Coreへ移植する。

### 8.6 NVIDIAバックエンド

#### NVDEC

```text
packet -> NVDEC -> CUDA array/device surface
```

- VP9/AV1対応を実行時に検出する。
- decoder outputはCUDA/D3D11 Surfaceとしてlease管理する。
- 対応環境ではblock-linear CUDA array direct outputを使用可能にする。

#### NVENC

本プロジェクトではAV1のみを対象とし、H.264/HEVCを公開しない。

```text
NumPy BGR -> pinned staging -> GPU upload -> NV12/P010 -> NVENC AV1
GPU frame -> GPU conversion(optional) -> NVENC AV1
NVDEC CUarray -> NVENC AV1
```

- NVENC APIを動的ロードする。
- CUDAまたはD3D11 resourceを登録・mapして投入する。
- input/output slotを4～8程度のリングとして再利用する。
- CUDA stream/eventまたはD3D同期を使用し、GPU全体同期を避ける。
- NVDECとNVENCのcontext/stream互換時はzero-copyを使用する。
- libwebmへ渡すため、圧縮済みbitstreamのみ必要に応じてCPUへ回収する。

### 8.7 非同期パイプライン

```text
demux -> decode submit -> decode completion -> processing/VPP
      -> encode submit -> encode completion -> mux
```

- 外部同期APIでも内部処理は並行可能とする。
- decode queue、Surface pool、encode queue、bitstream poolは上限付きとする。
- queue満杯時の既定動作はblock。
- SurfaceはGPU完了イベントまで返却しない。
- context全体を止める同期を通常経路で使用しない。
- Pythonから長時間処理・待機する間はGILを解放する。

### 8.8 モード既定値

初期の目安。実測により調整可能とする。

| mode | prefetch | async depth | queue size | 目的 |
|---|---:|---:|---:|---|
| low_latency | 1-2 | 1-2 | 1-2 | 遅延優先 |
| balanced | 4 | 4 | 8 | 標準 |
| throughput | 8-16 | 6-8 | 16 | オフライン処理 |

CPU backendでは `async_depth` をworker queue、codec内部thread、lag設定へ読み替える。

### 8.9 メモリ所有権

- `write(numpy)` は初期実装ではライブラリ所有バッファへ安全にコピーする。
- 将来のborrowed APIは、完了まで変更禁止という契約を別APIで明示する。
- GPU Surfaceはretain/release可能なleaseとする。
- 非同期consumerは完了まで内部参照を保持する。
- device/contextが異なるSurfaceの直接入力は禁止し、許可されたcopy経路またはエラーとする。

### 8.10 統計

デコード:

- demux時間、decode時間、Surface待ち時間
- prefetch hit/miss、queue最大深度
- 出力fps、drop数

エンコード:

- input/encoded fps
- queue待ち、upload、変換、encode、mux時間
- drop数、queue最大深度
- `input_path`: `cpu`, `cpu_upload`, `gpu_copy`, `zero_copy`

GPU時間は可能な限りGPU eventで計測し、CPU wall timeと区別する。

## 9. ビルド・配布

- CMakeを使用する。
- `mkvcodec_core` と各backendを分離する。
- GPU SDK未導入でもCPU版をビルド可能にする。
- 実行環境に対象GPUがなくてもライブラリをロード可能にする。
- Python wheelはWindows/Linuxを対象とする。
- C#配布はnative runtime別パッケージを想定する。
- libvpx、libwebm、libyuv、oneVPL、NVIDIA Video Codec SDK等のバージョンとライセンスを台帳化する。

## 10. 受け入れ基準

### 10.1 MVP

1. Windows x64およびLinux x64でCPU版ライブラリをビルドできる。
2. Python 3.12からC ABI経由のCoreをロードできる。
3. BGR `uint8` ndarrayをVP9へエンコードし、MKV/WebMへ保存できる。
4. 保存ファイルを本ライブラリと独立した検証ツールで最後までデコードできる。
5. 出力のcodec、解像度、fps、フレーム数、durationが指定値と一致する。
6. VP9 MKV/WebMをCPUでデコードし、BGR/I420フレームを取得できる。
7. `with`を抜けた後にファイルがfinalizeされ、再生・シーク可能である。
8. `close()`、`release()`、destroyは複数回呼んでもクラッシュしない。
9. 不正shape、dtype、奇数解像度、非対応codec/backendに明確なエラーを返す。
10. キュー上限を超えてメモリが無制限に増加しない。
11. C ABI create/write-or-read/destroyをC# P/Invokeスモークテストから実行できる。
12. 公開codec一覧にH.264/HEVCが現れず、指定しても受理しない。

### 10.2 Intel GPU

1. 対応Intel GPU上でoneVPL deviceとcodec capabilityを列挙できる。
2. 対応するVP9/AV1入力をGPU decodeできる。
3. 対応codecをGPU encodeし、有効なMKV/WebMを生成できる。
4. WindowsではD3D11、LinuxではVA-API Surfaceを取得・解放できる。
5. `async_depth > 1`で複数処理を投入しても順序、PTS、映像内容が壊れない。
6. 同一deviceのdecode→encodeで選択された経路を統計に正しく表示する。
7. Surface解放を意図的に遅らせても、使用中Surfaceが再利用されない。

### 10.3 NVIDIA GPU

1. NVIDIA GPUがない環境でもライブラリのロードとCPU backend利用が可能である。
2. 対応GPU上でNVDEC/NVENC capabilityを列挙できる。
3. NVDECでVP9/AV1をデコードできる。
4. AV1 NVENC対応GPUでAV1 MKVを生成し、独立したdecoderで最後まで読める。
5. AV1 NVENC非対応GPUでは、理由を含むエラーまたは仕様に沿った別backendを選択する。
6. `backend="nvenc", codec="vp9"` は明確に拒否する。
7. NVDEC→NVENC direct path対応環境では `input_path="zero_copy"` を報告する。
8. `require_zero_copy=True` でdirect pathが成立しない場合はコピーへ黙って降格しない。
9. 非同期depthを上げてもフレーム順、PTS、Surface寿命が正しい。

### 10.4 ABI・言語境界

1. C++例外がC ABI外へ漏れない。
2. すべての失敗で安定したエラーコードと詳細文字列を取得できる。
3. DLL内メモリを対応するDLL APIで解放できる。
4. 異なるバインディングから同じCore機能を利用できる。
5. 互換versionの構造体は `struct_size` に基づいて安全に処理できる。
6. Python処理待機中にGILが解放され、別Python threadが進行できる。

## 11. テスト仕様

### 11.1 Unit test

- codec/backend選択とフォールバック順
- H.264/HEVC拒否
- capability filter
- PTS/frame-index相互変換
- nanosecondsとMatroska timecode変換
- keyframe情報
- queueのpush/pop/close/timeout
- queue満杯時のblockとキャンセル
- Surface retain/releaseと二重release
- Surface pool枯渇と復帰
- frame shape、dtype、stride、plane検証
- BGR/I420/NV12変換
- odd width/heightの扱い
- config既定値と範囲チェック
- C ABI null pointer、不正handle、不正version
- thread-local error
- `close()`/destroyの冪等性
- muxer finalizeとエラー中断

### 11.2 CPU integration test

- 既知パターン映像をlibvpx VP9 encode
- 生成MKV/WebMを再demux/decode
- フレーム数、PTS、duration、解像度、pixel format検証
- encode→decode round tripのPSNR/SSIMまたは許容誤差検証
- 固定色、カラーバー、移動パターン、ランダムノイズ
- 1フレーム、短尺、キーフレーム間隔超え長尺
- `prefetch=0/1/4/16`
- queue size最小・通常・大
- `low_latency/balanced/throughput`
- flush前後の遅延フレーム欠落検査
- 非連続stride ndarray
- read/write batch
- 反復open/close
- 複数ストリーム並行実行
- CPU thread oversubscription監視

### 11.3 Intel hardware test

- device列挙とcapability照合
- VP9/AV1 decode（対応時）
- VP9/AV1 encode（対応時）
- CPU input→GPU upload→encode
- GPU Surface decode→encode
- VPP色変換・リサイズ
- Windows D3D11 Surface
- Linux VA-API Surface
- AsyncDepth 1/2/4/8
- Surface pool最小構成と枯渇
- GPU copy/zero-copy判定
- driver/device loss時のcleanup

### 11.4 NVIDIA hardware test

- NVIDIA DLL/driverの動的検出
- NVDEC VP9/AV1
- NVENC AV1 8-bit、および対応時10-bit
- NumPy BGR→upload→GPU変換→NVENC
- CUDA NV12/P010→NVENC
- D3D11 Texture→NVENC（Windows）
- NVDEC→NVENC CUarray direct path
- 異なるCUDA context/deviceの拒否またはcopy
- CUDA stream/event同期
- AsyncDepth 1/2/4/8
- 非対応世代での正しいエラー
- NVENC VP9拒否
- driver reset/device loss時のcleanup

### 11.5 C ABI / Python / C# test

- Cからcreate/read/write/flush/destroy
- Python context managerと例外変換
- Python ndarrayのdtype/shape/stride
- Python GC中のSurface寿命
- GIL解放確認
- C# P/Invokeロード
- C# `SafeHandle`/`IDisposable`
- C#例外変換
- UTF-8およびWindows Unicodeパス
- 32/64-bit型サイズとstruct layout検証

### 11.6 ファイル互換性テスト

- 独立ツールによるcodec、track、duration、PTS検査
- 複数の一般的なplayer/decoderでの再生確認
- MKVとWebMの両拡張子
- キーフレーム位置とシーク確認
- 正常終了ファイルの完全デコード
- 中断ファイルの挙動確認
- 大容量ファイルと長時間PTS

### 11.7 性能・安定性テスト

絶対性能値は対象hardware別にbaselineを記録し、回帰率で管理する。

- 1080p/30、1080p/60、4K/30、可能なら4K/60
- decodeのみ、encodeのみ、transcode
- CPU、Intel、NVIDIAの各経路
- latency、throughput、CPU使用率、GPU使用率
- peak RAM/VRAM
- queue wait、upload、convert、codec、muxの時間内訳
- 30分以上の連続処理
- 数百回のopen/close
- キャンセル、例外、ディスクfull相当
- memory leak、handle leak、Surface leak
- prefetch/async depth変更時の性能曲線
- zero-copy、GPU copy、CPU uploadの比較

性能受け入れ条件:

1. 同一条件の直近baselineに対し、理由のない重大な性能回帰がない。
2. queueを増やしてもRAM/VRAM使用量が設定上限に従う。
3. `balanced`でpipeline各段が並行動作し、毎フレーム全面同期になっていないことをtraceで確認できる。
4. zero-copy経路で映像pixelのCPU round-tripが発生しないことを計測またはtraceで確認できる。
5. 長時間処理後もメモリ・handle・Surface数が継続増加しない。

## 12. CI方針

```text
通常CI:
  Windows/Linux build
  Unit test
  CPU libvpx integration
  C ABI test
  Python test
  C# smoke test
  MKV構造検証

Intel GPU runner:
  oneVPL hardware integration

NVIDIA GPU runner:
  NVDEC/NVENC hardware integration
```

hardware機能が存在しない場合は単純skipだけでなく、capabilityが「利用不可」と正しく報告されることをテストする。

## 13. 実装フェーズ

1. 共通データ型、C++ Core、C ABI、エラー処理
2. libwebm demux/mux、PTS、finalize
3. libvpx VP9 CPU encode/decode
4. Python OpenCV風API、C# ABIスモークテスト
5. 既存oneVPL decoderのCore統合
6. oneVPL encoderとD3D11/VA-API Surface
7. NVDEC
8. NVENC AV1
9. GPU copyとzero-copy transcode
10. C#高レベルラッパー
11. CPU AV1（SVT-AV1/libaom）の統合と音声等の将来拡張

## 14. 既存実装の扱い

既存 `vpl_wrapper` から次を再利用・移植する。

- oneVPL decoder
- libwebm VP9 demuxer
- `SurfaceFrame` lease
- CPU/GPU memory mode
- bounded prefetch queue
- batch read
- device列挙
- pybind11、型ヒント、テスト構成

既存のLinux VA-API中心APIを共通 `VideoFrame` へ一般化し、Windows D3D11、CUDA、CPU frameを同じCoreで扱えるようにする。

## 15. 確定した製品・依存・配布仕様

### 15.1 名称

> `MKVCodec` は現時点では作業名である。Matroska公式の名称・商標方針を踏まえ、商用公開前に名称利用を確認するかneutralなbrand名へ変更する。詳細は `LICENSE_POLICY.md` を参照する。

| 対象 | 名称 |
|---|---|
| 製品・リポジトリ | `MKVCodec` |
| Python distribution/package | `mkvcodec` |
| C/C++ namespace | `mkvcodec` |
| 共有ライブラリ | Windows: `mkvcodec.dll` / Linux: `libmkvcodec.so` |
| C ABI symbol prefix | `mkvc_` |
| C# namespace / NuGet package | `MkvCodec` |

公開C symbolは `mkvc_decoder_create` のように命名する。`mkv_` は一般的すぎるため使用しない。公開前にPyPI、NuGet、主要コードホスティング上の名称衝突を再確認する。C ABI prefixは最初の公開リリース後は変更しない。

### 15.2 CPU AV1 backend

CPU AV1は次の役割分担を採用する。

| 処理 | backend | 理由 |
|---|---|---|
| AV1 encode | SVT-AV1 | 動画エンコードの速度・並列性能を優先 |
| AV1 decode | libaom | 標準的なAV1 software decoderとして採用 |

外部backend名は `svtav1` と `libaom` とする。`backend="cpu"` または `auto` は処理方向に応じて選択する。

```text
AV1 CPU encode -> SVT-AV1
AV1 CPU decode -> libaom
VP9 CPU encode/decode -> libvpx
```

SVT-AV1はAPI非互換を含むmajor releaseがあるため、初期対応系列を `4.x` としてCIで固定する。更新時は画質・性能・ABI試験を必須とする。

### 15.3 rate controlと既定値

共通APIはbackend固有値を直接共通化せず、共通設定から各backendへ変換する。

```python
rate_control="quality"
quality=32                    # 0が最高品質、63が最低品質
bitrate_kbps=None
max_bitrate_kbps=None
mode="balanced"
keyframe_interval_seconds=4.0
threads=0                     # 自動
lag_in_frames=None            # mode/backendにより自動
```

- `quality` の範囲は0～63とする。
- quality modeではbitrateを指定しない。
- `rate_control="vbr"` では `bitrate_kbps` を必須とする。
- `rate_control="cbr"` は対応backendのlow-latency用途のみ許可する。
- backend間の同じquality値は近似であり、画質やbitstreamの一致を保証しない。
- 詳細設定は `backend_options` に隔離する。

balancedの初期マッピング:

| Backend | Codec | 初期設定 |
|---|---|---|
| libvpx | VP9 | CQ、`cq-level=quality`、`cpu-used=6`、row-mt有効 |
| SVT-AV1 | AV1 | CRF=`quality`、preset 8 |
| oneVPL | VP9/AV1 | 利用可能なquality-based RC、TargetUsage balanced相当 |
| NVENC | AV1 | preset P4、HQ tuning、VBR/CQ相当、lookaheadはcapability依存 |

`low_latency` はlookahead/lagを0にしてキューを浅くする。`throughput` は高速presetと深いpipelineを使用する。既定値を変更する場合はrelease noteへ記載する。

### 15.4 WebMとMKVの機能差

出力拡張子でDocTypeを決定する。

| 項目 | `.webm` | `.mkv` |
|---|---|---|
| DocType | `webm` | `matroska` |
| VP9 / AV1映像 | 許可 | 許可 |
| 初期リリースの音声・字幕 | 非対応 | 非対応 |
| 将来の音声 | Opus/Vorbisのみ | Matroskaで許可するcodec |
| 将来の字幕・添付・章 | 非対応 | 対応候補 |

初期リリースは映像1トラックのみなので、実質差はDocTypeとvalidationである。`.webm` はWebM subset外の要素を拒否する。`.mkv` でも未実装機能を黙って無視しない。拡張子と明示 `container=` が矛盾する場合はエラーとする。

### 15.5 NumPy所有権

初期リリースではborrowed NumPy APIを公開しない。

```python
writer.write(frame)  # ライブラリ所有のstaging bufferへコピー
```

非同期処理中の変更、GC、非連続strideによる不具合を避けるためである。コピー回避には `write_surface()`、I420/NV12入力、batch APIを使用する。将来追加する場合は別名のAPIと完了tokenを用意し、token完了まで配列を変更・解放してはならない契約とする。

### 15.6 drop policy

初期リリースはファイル完全性を優先し、`overflow="block"` のみ正式対応する。queue満杯時に暗黙破棄しない。

- `drop_newest` / `drop_oldest` は初期対象外。
- 非blocking用途には `try_write()` を用意し、満杯なら `False` / `WOULD_BLOCK` を返す。
- ライブ用途のdropはPTS補正、統計、キーフレーム制御を含む将来機能とする。

### 15.7 SDK最低version

| 依存 | 最低version/系列 | 方針 |
|---|---|---|
| oneVPL Dispatcher API | API 2.10以上 | 実行時にimplementation capabilityを確認 |
| Intel GPU runtime | 検証済み現行driver | versionだけでなくcodec queryを必須化 |
| NVIDIA Video Codec SDK | 13.1 | AV1とCUDA array direct pathを基準化 |
| NVIDIA driver | SDK 13.1の要求以上 | 実行時API negotiationで検証 |
| CUDA Toolkit（ビルド時） | 13.1系列 | CUDA backend有効ビルドのみ必要 |
| SVT-AV1 | 4.x | minor/patchをlockfileとCIで固定 |
| libvpx | 1.17.x | minor/patchをlockfileとCIで固定 |

最低versionは再現可能なビルド/API契約の基準である。実際のcodec対応は必ず実行時capabilityで判定する。古いNVIDIA SDK互換が必要になった場合は別build profileとし、主ABIを複雑化させない。

### 15.8 wheel/NuGetへの同梱範囲

基本方針は「OSSのCPU依存は同梱し、vendor driver/runtimeは同梱しない」とする。

同梱対象:

- MKVCodec Core、C ABI共有ライブラリ、言語binding
- libwebm、libvpx、libyuv
- SVT-AV1、libaom（CPU AV1を含む配布物）
- oneVPL Dispatcher（ライセンスとplatform packaging規約に適合する場合）
- LICENSE、NOTICE、PATENTS、version manifest

同梱しないもの:

- Intel GPU driver/runtime
- NVIDIA display/CUDA driver
- NVIDIA Video Codec SDK一式とsample source
- OSのD3D11/VA-API components

GPU backendはdriver APIを動的ロードする。GPUがない環境でもimport/loadに成功し、capability APIが利用不可理由を返すことを必須とする。CUDA Toolkitをエンドユーザーの必須要件にせず、原則として対応GPU driverだけで動作させる。

package分割は容量、ライセンス、CI運用上必要になった場合のみ行う。初期方針は単一の標準wheel/NuGetとする。

### 15.9 ライセンス・再配布ゲート

詳細なcomponent別評価、artifact収録物、Go/No-Go判定は `LICENSE_POLICY.md` を正とする。

H.264/HEVCを実装・公開・暗黙選択しない。ただしVP9/AV1やSDKについて法的保証を表明してはならない。

正式リリース前の必須成果物:

- `THIRD_PARTY_NOTICES.md`
- dependency名、version、取得元、license、同梱有無を記録したSBOM
- wheel/NuGet/zipごとの内容物一覧
- LICENSE/NOTICE/PATENTSファイルの同梱検査
- NVIDIA SDK由来code/header/binaryの配布可否レビュー記録
- oneVPL/Intel runtimeの再配布範囲レビュー記録
- libvpx、libwebm、libyuv、libaom、SVT-AV1のnotice検査
- 必要と判断された場合の専門家によるcodec特許方針確認

NVIDIA Video Codec SDKのライセンスはSDK利用とは別に映像codecの特許権等を付与しない旨を示しているため、SDK再配布とcodec権利確認を分けて扱う。これは法的助言ではなく、リリース工程上の確認ゲートである。

配布可否が未確認のvendor componentは同梱せず、利用者環境のruntimeを動的ロードする。レビュー完了前に正式公開releaseを作成しない。

## 16. 将来検討事項

- 音声、字幕、複数track、章、添付ファイル
- borrowed NumPy APIと完了token
- ライブ用途のdrop policy
- CPU AV1依存を除いた軽量配布variant
- 古いNVIDIA SDK/driver向け互換build profile
- ARM64 Linux/Windows

