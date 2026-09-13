# Implementation status

この文書はMKVCodecの現在の実装・検証状況を短く示す。仕様上の要求と設計は
外部仕様・内部仕様・テスト要求・traceabilityを正本とし、実装過程の詳細、
過去時点の未実装表現、測定値、完全なverification matrixは
[implementation-history.md](implementation-history.md)に保持する。

## Qualification summary

| Environment | Current suite | Result |
|---|---:|---|
| Windows x64 / NVIDIA-enabled build | 38 tests | pass、GPU capability不足による想定内skip 2 |
| Windows x64 / CPU VP9+AV1、Intel、NVIDIA同時build | 33 tests | pass、実hardware capability不足による想定内skip 8 |
| Linux x64 / Intel GPU required | 53 tests | pass、C++/.NET GPU round-tripを含む |
| Linux x64 / CPU VP9 and AV1 | 32 tests | pass |
| GitHub Ubuntu / ASan+UBSan | 27 tests + 60秒libFuzzer | pass、11,888,127 inputs |

各refactorではABI guard、Python/.NET binding generation、docgen、source
compliance gate、対象PythonのRuff検査も通す。性能値は環境依存であり、承認済み
regression thresholdではない。

## Current capability matrix

| Area | Implemented | Current boundary |
|---|---|---|
| Container | libwebmによるWebM/Matroska mux・demux、拡張子とDocTypeの整合、VP9/AV1自動判別、decode不要の動画情報probe、共通入力上限、固定破損＋seeded mutation smoke、ASan/UBSan＋辞書付きlibFuzzer CI、run別corpus/crash artifact保存 | 最初の対応映像trackを選択。hardware-class間のcorpus統合は継続課題 |
| CPU codec | libvpx VP9 encode/decode、SVT-AV1 encode、libaom AV1 decode、PSNR＋block SSIM受け入れ | 現行は8-bit I420/NV12/packed入力。10-bitは将来範囲 |
| CPU Python | OpenCV風Capture/Writer、owned/borrowed NumPy、pageable/page-locked native buffer pool、async submission、pool occupancy/wait/lease metrics | 長時間GC/memory soakと詳細copy traceは残件 |
| Intel Linux | oneVPL VP9/AV1、VA surface、OpenCL/Level Zero、device-USM DLPack、pool/backpressure | direct oneVPL USM consumptionと30分cross-context soakは残件 |
| Intel Windows | D3D11 handle/fence契約と外部import実装 | 実GPUでのdecode→external processing→encode総合認定が残件 |
| NVIDIA Windows | NVDEC VP9 CUDA surface、CUDA pointer/array/event interop、NVENC AV1実装 | RTX 2060はAV1 encode非対応。対応GPUでのpositive NVENC試験が必要 |
| GPU strict mode | 共通GpuFrame lease、native handle、DLPack、`require_gpu_resident`、copy-path metrics | driver内部まで含む完全copy proofは環境別に継続 |
| C++ | move-only RAII facade、CPU/GPU frame、pool、submission、strict GPU transcode harness | Linux Intel round-trip認定済み。AV1対応NVIDIAでのpositive encode認定が残件 |
| .NET | .NET 8 P/Invoke、SafeHandle、codec自動判別・動画情報probe、全8-bit CPU形式、pageable/page-locked native pool、Submit＋cancellable `WaitAsync`、GPU surface API、strict GPU transcode harness | 長時間GC pressure認定、Windows Intel round-tripが残件。AV1対応NVIDIA実機認定は対応hardware待ち |
| Observability | aggregate queue/backend/copy-path metrics、versioned frame/flush/close host timing、同期/worker内訳、conversion/codec/container/GPU-waitの排他的host timing、JSON benchmark、相対baseline gate | device kernel/event timingとdriver内部copy追跡は残件 |
| Packaging | dependency manifest、legal payload collector、SPDX SBOM、wheel/NuGet builderとinspector、通常/delay-load PE importの再帰閉包検査、qualification/release分離、Windows全backend実artifact load・CPU実行認定 | project LICENSE決定、実Intel/NVIDIA AV1 hardware認定、公開前legal reviewとrelease artifactが残件 |

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

1. AV1対応NVIDIA GPUでNVDEC/NVENC positive GPU-resident transcodeを認定する。
2. Intel Windows D3D11の実機end-to-endとmanaged/C++ hardware round-tripを認定する。
3. Intel direct oneVPL USM consumption、cross-context fault、30分pool soakを完了する。
4. project LICENSEを決定し、全backend構成の配布物について最終legal reviewを行う。
5. 性能baselineと許容regression thresholdを承認する。

## Verified dependency baseline

- vcpkg registry `114d9fe62faf35856b45cf55cb93b57028a45d63`
- libvpx `1.16.0#3`、libwebm `1.0.0.32`、libyuv `1916`、libjpeg-turbo `3.2.0`
- Highway `1.4.0`、SVT-AV1 `4.1.0`、libaom `3.15.0`
- oneVPL dispatcher/headers `2.17.0`
- nv-codec-headers `n13.1.15.0`

より詳細なdriver/runtime版、host構成、個別試験、過去の測定値は
[implementation-history.md](implementation-history.md)を参照する。
