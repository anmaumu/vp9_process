# Implementation status

この文書はMKVCodecの現在の実装・検証状況を短く示す。仕様上の要求と設計は
外部仕様・内部仕様・テスト要求・traceabilityを正本とし、実装過程の詳細、
過去時点の未実装表現、測定値、完全なverification matrixは
[implementation-history.md](implementation-history.md)に保持する。
v0.1に必要な残件は[release-closure.md](release-closure.md)だけを正本とする。

## Qualification summary

| Environment | Current suite | Result |
|---|---:|---|
| Windows x64 / NVIDIA-enabled build | 38 tests | pass、GPU capability不足による想定内skip 2 |
| Windows x64 / CPU VP9+AV1、Intel、NVIDIA同時build | 36 tests | pass、実hardware capability不足による想定内skip 8 |
| Linux x64 / Intel GPU required | 59 tests | pass、C++/.NET GPU round-trip、USM fault report、component timer回帰を含む |
| Linux x64 / CPU VP9 and AV1 | 32 tests | pass |
| GitHub Ubuntu / ASan+UBSan | 27 tests + 60秒libFuzzer | pass、11,888,127 inputs |

各refactorではABI guard、Python/.NET binding generation、docgen、source
compliance gate、対象PythonのRuff検査も通す。性能値は環境依存であり、Windows
Xeon CPUとLinux Arc B580だけを機種・driver・入力hash固定の承認済みbaselineとする。

## Current capability matrix

| Area | Implemented | Current boundary |
|---|---|---|
| Container | libwebmによるWebM/Matroska mux・demux、拡張子とDocTypeの整合、VP9/AV1自動判別、decode不要の動画情報probe、共通入力上限、固定破損＋seeded mutation smoke、ASan/UBSan＋辞書付きlibFuzzer CI、run別corpus/crash artifact保存 | 最初の対応映像trackを選択。hardware-class間のcorpus統合は継続課題 |
| CPU codec | libvpx VP9 encode/decode（0指定時は1..16 bounded自動thread）、SVT-AV1 encode、libaom AV1 decode、PSNR＋block SSIM受け入れ | 現行は8-bit I420/NV12/packed入力。10-bitは将来範囲 |
| CPU Python | OpenCV風Capture/Writer、owned/borrowed NumPy、strict/copy-normalized layout policy、pageable/page-locked native buffer pool、async submission、pool occupancy/wait/lease metrics、edge別copy/share metrics | driver内部copy attributionは観測範囲外 |
| Intel Linux | oneVPL VP9/AV1、認定済みVA surface、OpenCL/Level Zero、preview device-USM DLPack、pool/backpressure | USMはopaque context/device provenanceをcore単独で検証できないためv0.1 preview。direct oneVPL USM consumptionは将来範囲 |
| Intel Windows | D3D11 handle/fence契約と外部import実装（v0.1 preview/unqualified） | 実GPUでのdecode→external processing→encode総合認定後にstable対応表へ追加する |
| NVIDIA Windows | NVDEC VP9 CUDA surface、CUDA pointer/array/event interop、NVENC AV1実装（AV1 encodeはv0.1 preview/unqualified） | RTX 2060はAV1 encode非対応。対応GPUでのpositive NVENC試験後にstable認定する |
| GPU strict mode | 共通GpuFrame lease、native handle、DLPack、`require_gpu_resident`、aggregateおよびedge別copy-path metrics | driver内部まで含む完全copy proofは環境別に継続 |
| C++ | move-only RAII facade、CPU/GPU frame、pool、submission、strict GPU transcode harness | Linux Intel round-trip認定済み。AV1対応NVIDIAでのpositive encode認定が残件 |
| .NET | .NET 8 P/Invoke、SafeHandle、codec自動判別・動画情報probe、全8-bit CPU形式、pageable/page-locked native pool、Submit＋cancellable `WaitAsync`、GPU surface API、strict GPU transcode harness、GC/memory soak harness、30分CPU pool認定 | Windows Intel round-tripが残件。AV1対応NVIDIA実機認定は対応hardware待ち |
| Observability | aggregate queue/backend/copy-path metrics、shared/zero-copy/GPU-copy/upload/readback/normalization/pixel-conversion別count、driver内部未観測表示、versioned frame/flush/close host timing、同期/worker内訳、conversion/codec/container/GPU-waitの排他的host timing、JSON benchmark、相対baseline gate | device kernel/event timingとdriver内部copy attributionはlibrary外の認定作業 |
| Packaging | dependency manifest、artifact固有legal payload/SPDX SBOM、wheel/NuGet builderとinspector、通常/delay-load PE importの再帰閉包検査、qualification/release分離、Windows全backend実artifact load・CPU実行認定、公式manylinux_2_28 source build＋auditwheel修復前後検査＋Python 3.9隔離import（生成wheelはmanylinux_2_27にも適合） | project LICENSE決定、実Intel/NVIDIA AV1 hardware認定、公開前legal reviewとrelease artifactが残件。SYCL bridgeは汎用Core wheel外のruntime別companion |

H.264/HEVCは公開codec、暗黙fallback、配布対象のいずれにも含めない。
GPU vendor driver/runtimeはwheel/NuGetへ同梱せず、実行環境側の責任とする。

## Memory and processing model

- CPU decodeはowned I420、read-only borrowed NumPy、または明示的なpacked/NV12
  copyを返す。borrowed sliceは最後のNumPy viewまでnative leaseを保持する。
- CPU encodeは安全なcopy入力、同期borrowed入力、completion付き非同期borrowed入力、
  fixed-capacity native buffer poolを提供する。
- GPU decodeは共通opaque `GpuFrame`を返し、Intel/NVIDIA固有resourceをnative
  handleまたはDLPackとして外部処理ライブラリへ渡せる。
- GPU encodeは互換backend・device・layout・producer completionを検証した
  surfaceを受け取る。strict modeではCPU stagingを拒否する。
- resize、crop、基本色変換、rotate/flip、letterbox/pillarbox相当のcontain処理は
  CPU `read_processed()`で実装済み。GPU画像処理kernelは本ライブラリの責務外で、
  external processorへzero-copyで渡す方針とする。

## Public API stability

- C ABIは`mkvc_` prefix、opaque handle、versioned struct、result codeと
  thread-local error detailで固定する。
- Pythonは`VideoCapture`、`VideoWriter`、`VideoInfo`、`probe_video`、`GpuFrame`、`BorrowedCpuFrame`、
  `CpuFramePool`、`CpuBuffer`、`Submission`を公開する。
- Python/.NETのnormalized GPU interop情報は表現ごとの`api_stability`を返し、Intel USMを
  v0.1 previewとして機械判定可能にする。
- C++はstable C ABI上のheader-only RAII facade、.NETは同じABI上のP/Invokeを使う。
- Python public docstringはNumPy形式、C/C++宣言はDoxygen形式とし、docgenで検査する。

## Latest structural state

- root CMakeは93行のorchestrationに縮小し、dependency、library、core/backend test
  definitionsをmoduleへ分離した。
- GPU frameのdomain、C ABI、Intel/NVIDIA external importを別translation unitへ分離した。
- Python `GpuFrame`は公開lease/import APIと、descriptor/native-handle/completionの
  C ABI marshallingを分離した。
- Intel/NVIDIAのPython external GPU importは、versioned NV12 descriptor構築と
  owner lifetime transferを共通化し、backend固有部をvalidationと同期方式に限定した。
- External GPU importのscalar/frame-size検証はassemblyから分離し、公開例外messageを
  回帰テストで固定した。
- CPU frame processorは処理フローとlibyuv I420 primitivesを別translation unitへ
  分離し、将来のGPU processor追加時にbackend境界を共有しやすくした。
- Pythonは`GpuProcessor`/`GpuImage`/`VideoCapture.read_gpu`のbackend-neutral契約を公開し、
  Intel/NVIDIA adapterの選択、DLPack metadata、decode source retain、`gpu_copy`強制、
  CPU fallback禁止を共通化した。optional `nvidia-cupy` adapterはCUDA stream上の
  uint8 RGB/BGR/RGBA/BGRA HWC/CHW変換を実装し、RTX 2060のNVDEC 1080p60 600-frame
  実測で856.14 fps、同一matrix CPU oracleとの差は最大1であった。optional
  `intel-dpnp` adapterは線形device-USM NV12の`copy=False` DLPack inputと同一SYCL
  device上のuint8 packed変換を実装し、Arc B580で画素・PTS・lease・output
  DLPackを検証済み。Linux VA decode surface→線形USMのOpenCL GPU copyはqualification
  経路で実機成功し、CPU referenceに対するNV12および6色変換条件の差はすべて0であった。
  Arc B580の1080p/100-frame直列計測はVA→USM→RGB 172.85 fps、decode込み推定
  145.64 fpsであり、RGB変換の5.270 ms/frameが支配項であった。
  直接VA-NV12→packed-USM融合kernelをoptional `intel-opencl` product adapterへ昇格し、
  fixed-capacity pool/backpressure、OpenCL event completion、DLPack consumer leaseを実装した。
  Arc B580の異なる1080p decode frame 100枚×5 run中央値は1049.69 fps、画素差max=1である。
  same-process pool/VRAM soak harness、30分認定、wheel companion/legal/SBOM実artifact検査まで実装済みである。
  host waitを置き換えるcross-API event dependencyとWindows D3D11経路は残る。
- Container policy/finalizationは、bounded EBML header parse・size rewrite・atomic file
  replacementから分離した。
- CPU AV1 encoderはpublic lifecycle、mutable state、SVT-AV1 runtime処理を分離し、
  frame conversion・packet drain・EOS・mux finalizeの順序を維持した。
- CPU VP9 encoderもpublic lifecycle、mutable state、libvpx runtime処理を分離し、
  AV1 backendと同じ保守境界へ揃えた。
- Async failure testはprocess固有temporary directoryを使い、同一host上の複数build
  matrixを並列実行してもartifactが衝突しない。
- Intel oneVPL encoderはfacade stateとGPU surface submission adapterを分離し、
  timestamp・imported wrapper・completion・queue進行を一つの内部境界へ集約した。
- NVIDIA NVENC encoderはfacade lifecycle、mutable state、CPU/CUDA submissionを分離し、
  timestamp・GOP・CUDA context・mux・成功後counter更新を内部adapterへ集約した。
- GPU resource poolはC++のslot・generation・backpressure本体と、opaque handle・
  versioned descriptor・error mappingを担うC ABI translation unitへ分離した。
- Encoder queueはowned/borrowed producer submissionと、flush barrier・cancel・
  worker close制御を別moduleへ分離し、backpressureとcompletion契約を維持した。
- NVIDIA probeはcodec runtimeと同じRAII dynamic-library moduleを使用し、
  Windows/Linux固有のload・symbol・close処理の重複を解消した。
- Intel decoder queueはmutable state、DecodeFrameAsync submit、CPU/GPU collect・
  drain、close-time sync/releaseを別translation unitへ分離した。
- CPU VP9/AV1 decoderはpublic create/read/close lifecycle、mutable state、
  libvpx/libaom demux・decode・I420 extraction runtimeを分離した。
- Decoder pipelineはbackend access、prefetch worker/consumer、同期CPU/GPU readを
  分離し、pipeline本体をmode dispatchとclose順序の調整へ限定した。
- CPU C ABIはsubmission lifetime/encoder bridge、frame pool create/acquire、
  buffer descriptor/view/releaseを3つのtranslation unitへ分離した。
- Python writerはframe-view/config構築、captureはoutput copy/process plan構築、CPU
  ownershipはdecoded lease/pool/submissionへ分離した。
- Intel USMはnative reservation、writable slot/ownership transfer、pool/backpressureを
  分離し、Arc B580の実USM/DLPack/VA/oneVPL往復で再認定した。
- 公開API、ABI fingerprint、test名、fixture名はこれらの分割で変更していない。

## Deferred feature scope

- Decoder seekは初期リリース対象外で、現行APIは先頭からEOSまでの逐次readのみを
  保証する。将来はtimestamp基準のprevious-keyframe/exact seekを先に設計し、
  frame-index seekはVFR・frame count契約の確定後に検討する。

## Remaining acceptance work

残件は[release-closure.md](release-closure.md)の`RC-01..07`へ統合した。
10-bit、seek、全driver内部copy証明、全GPU世代・4K matrix等はv0.1完成条件から外す。
実機を用意できないIntel Windows/NVIDIA AV1はv0.1でpreview/unqualifiedとしてstable
対応表から外し、実機認定を将来backlogとして保持する。

## Verified dependency baseline

- vcpkg registry `114d9fe62faf35856b45cf55cb93b57028a45d63`
- libvpx `1.16.0#3`、libwebm `1.0.0.32`、libyuv `1916`、libjpeg-turbo `3.2.0`
- Highway `1.4.0`、SVT-AV1 `4.1.0`、libaom `3.15.0`
- oneVPL dispatcher/headers `2.17.0`
- nv-codec-headers `n13.1.15.0`

より詳細なdriver/runtime版、host構成、個別試験、過去の測定値は
[implementation-history.md](implementation-history.md)を参照する。
