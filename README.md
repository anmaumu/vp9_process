# MKVCodec (`vp9_process`)

VP9/AV1専用のWebM/Matroska動画codecライブラリです。安定したC ABIを中核に、
Python、C++17、.NET 8から同じdecode/encode機能を利用できます。

- PythonはOpenCVの`VideoCapture` / `VideoWriter`に近い操作感
- CPUではNumPy/OpenCV frame、borrowed frame、非同期native buffer poolに対応
- GPUではdecode surfaceをhostへ戻さず、外部GPU処理へ渡してencodeへ戻せる
- Windows x64とLinux x86_64を対象
- H.264/HEVCは実装・配布対象外

> v0.1は技術認定をほぼ完了しています。公開前の必須残件はproject LICENSEの決定と、
> そのLICENSEを含むwheel/NuGet実artifactの最終legal reviewです。
> 詳細は[release closure](docs/release-closure.md)を参照してください。

## 対応範囲

| 経路 | Windows | Linux | v0.1での扱い |
|---|---|---|---|
| CPU VP9 encode/decode | 対応 | 対応 | Stable |
| CPU AV1 encode/decode | 対応 | 対応 | Stable |
| Intel oneVPL VP9/AV1 | Preview | 対応 | Linux Stable、Windows未認定 |
| NVIDIA NVDEC VP9 decode | 対応 | build可能 | Windows実機認定済み |
| NVIDIA NVDEC AV1 / NVENC AV1 | 実装済み | build可能 | 対応GPU実機がないためPreview |
| Intel VA-API surface interop | ― | 対応 | Stable |
| Intel D3D11 texture interop | 実装済み | ― | Preview |
| NVIDIA CUDA pointer/array/event | 対応 | build可能 | pointer/DLPack認定済み、AV1 encodeはPreview |
| Intel device-USM / DLPack | ― | 対応 | Preview |

実際のcodec対応はGPU、driver、runtimeによって異なります。ライブラリはruntime queryに
成功したcodecと方向だけを公開し、指定したGPU経路をCPUへ黙ってfallbackしません。

Containerはlibwebmを使用します。出力拡張子がcontainerを決定し、`.webm`は
EBML DocType `webm`、`.mkv`は`matroska`です。入力時も拡張子とDocTypeの矛盾を
エラーにします。

## Python quick start

初めて使う場合は、実行結果を確認しながら学べる
[Python入門Notebook](notebooks/python_beginner_tutorial.ipynb)も利用できます。

### CPU encode/decode

```python
import mkvcodec

with mkvcodec.VideoWriter(
    "output.webm",
    codec="vp9",                 # "av1"も選択可能
    fps=30,
    frame_size=(1920, 1080),
    quality=32,
    queue_size=8,                 # 0: 同期、正数: bounded非同期queue
) as writer:
    writer.write(bgr_ndarray)     # uint8 HxWx3、OpenCV形式

with mkvcodec.VideoCapture(
    "output.webm",
    codec="auto",                # container trackからVP9/AV1を判別
    prefetch=4,                   # 0: 同期、正数: bounded先読み
    conversion_threads=0,         # 0: 自動
) as capture:
    print(capture.info)           # codec、size、fps、duration、frame count
    while (frame := capture.read()) is not None:
        process(frame)
```

CPU WriterはBGR、RGB、BGRA、I420、NV12を受け付けます。CaptureもBGR/RGB/BGRA/
I420/NV12を返せます。`threads`はdecoder内部thread数、`prefetch`はdecode済みframeの
先読み数、`conversion_threads`はpacked色変換のthread数で、それぞれ独立しています。

### CPUのcopyを減らす

`read_borrowed()`はnative decode bufferをread-only NumPy viewとして公開します。
非同期入力ではmanaged arrayを長時間pinせず、固定容量`CpuFramePool`を使えます。

```python
with mkvcodec.VideoCapture("input.webm", prefetch=0) as capture:
    with capture.read_borrowed() as frame:
        result = process_cpu(frame.planes)

pool = mkvcodec.CpuFramePool("i420", (1920, 1080), capacity=4, page_locked=True)
with mkvcodec.VideoWriter(
    "output.webm", fps=30, frame_size=(1920, 1080), queue_size=4,
) as writer:
    buffer = pool.acquire()
    process_into(*buffer.planes)
    submission = writer.submit_buffer(buffer)
    buffer.close()                # submissionがslot leaseを保持
    submission.wait()
pool.close()
```

CPU入力は既定で有効なrow-strided配列を必要に応じてcopy正規化します。copyを許可しない
場合は`strict_cpu_layout=True`と必要な`required_alignment`を指定してください。

### GPU-resident transcode

```python
import mkvcodec

with mkvcodec.VideoCapture(
    "input.webm",
    codec="vp9",
    backend="intel",
    prefetch=0,
    require_gpu_resident=True,
) as capture, mkvcodec.VideoWriter(
    "output.webm",
    codec="av1",
    backend="intel",
    fps=30,
    frame_size=(1920, 1080),
    queue_size=0,
    require_gpu_resident=True,
) as writer:
    while (surface := capture.read_surface()) is not None:
        with surface:
            print(surface.interop)
            writer.write_surface(surface)
```

`GpuFrame`はbackend固有resourceを共通leaseとして扱います。外部処理へ渡せる表現は
`surface.interop` / `surface.supports_interop()`で確認できます。

- NVIDIA: CUDA device pointer、CUDA array、CUDA event、DLPack/CuPy
- Intel Windows: D3D11 texture/fence
- Intel Linux: VA surface、OpenCL/SYCL連携、device-USM/DLPack

外部処理後のresourceはproducer event/fenceとownerを付けて再importできます。最終leaseと
consumer処理が完了するまでresourceは再利用されません。`require_gpu_resident=True`では
CPU upload/readbackを必要とする操作を拒否します。

Intel device-USMはv0.1 Previewです。`frame.interop.api_stability == "preview"`で
機械判定できます。native coreはSYCL context/queueをopaque identityとして扱うため、
pointerが同じoneAPI context/deviceに属することをcaller側で検証してください。

## 画像処理の責務

本ライブラリはcodec/containerとCPU/GPU resourceの所有権・同期を担当します。

- CPU convenience APIにはcrop、resize、基本色変換、rotate/flip、contain/coverを実装済み
- GPU kernelは内蔵せず、CuPy、NPP、D3D11、VA-API、OpenCL、SYCL等へ渡す
- GPU frameをNumPy/OpenCVへ変換するときはdownload/copyが発生する
- GPU-resident surface同士はnative handle/DLPackとcompletionを使って受け渡す

library境界で観測したshared surface、GPU copy、CPU upload/readback、layout正規化、
pixel変換は`copy_edge_metrics`へ記録します。vendor driver内部のcopyは観測対象外であり、
counterが0でもdriver全体の完全なzero-copy証明にはなりません。

## C++ / .NET

C++17は`include/mkvcodec/mkvcodec.hpp`のmove-only RAII facadeを利用できます。DLL境界へ
C++ ABIは公開せず、すべて`mkvc_` prefixのC ABIを通ります。

.NET 8はP/Invoke、SafeHandle、`IDisposable`、CPU/GPU Capture/Writer、native pool、
非同期`MkvSubmission.WaitAsync()`を提供します。

```csharp
using var capture = new MkvVideoCapture(
    "input.webm", MkvCodecKind.Vp9, MkvBackend.Intel,
    prefetch: 0, requireGpuResident: true);
using var writer = new MkvVideoWriter(
    "output.webm", 1920, 1080, codec: MkvCodecKind.Av1,
    backend: MkvBackend.Intel, queueSize: 0, requireGpuResident: true);

while (capture.ReadSurface() is { } surface)
{
    using (surface) writer.WriteSurface(surface);
}
```

## Build and test

```shell
cmake --preset default
cmake --build --preset default
ctest --preset default
```

Backend別のSDK、driver、CMake optionと認定済み環境は
[実装状況](docs/implementation-status.md)および
[内部設計](docs/internal-spec/system-design.md)を参照してください。開発時は
`MKVC_LIBRARY_PATH`でbuild済みnative libraryを指定できます。

主要な検証状況：

- Linux Intel GPU suite: 59/59 pass
- Intel device-USM pool: 30分、8,288 frames、RSS/FD/thread/VRAM gate pass
- Intel Arc B580 VP9 decode→AV1 encode: 1080p中央値183.81 fps
- Windows CPU VP9とLinux Intel GPUの1080p回帰baselineを登録済み
- GitHub ActionsでDocumentation、ABI/binding、ASan/UBSan、bounded fuzzを実行

性能測定方法と環境固定条件は[benchmarking](docs/benchmarking.md)、全受け入れ条件は
[test requirements](docs/test-spec/test-requirements.md)を参照してください。

## Documentation

Python public APIはNumPy形式docstring、C/C++はDoxygen形式コメントを使用します。

```shell
python tools/docgen.py check
python tools/docgen.py generate
python -m pip install -r requirements-docs.txt
python tools/docgen.py build
```

生成Markdownは`build/docgen-src`、MkDocs/Doxygen HTMLは`build/docsite`へ出力されます。
CIはHTML siteをartifactとして保存します。

## Packaging and license

wheel/NuGet builderはnative library、third-party legal payload、SPDX SBOMを収録し、
再帰的native dependency、禁止codec、vendor runtime混入をfail-closedで検査します。
NVIDIA/Intel driver runtimeはpackageへ同梱しません。

現在はproject LICENSEが未決定のため正式artifactを公開できません。
`--qualification-only`で作るartifactは検査専用で、通常release gateを意図的に通りません。
LICENSE決定後の作成・検査契約は
[外部仕様](docs/external-spec/system-spec.md)と
[受け入れ・テスト仕様](docs/test-spec/test-requirements.md)を参照してください。

## 仕様・設計資料

- [外部仕様](docs/external-spec/system-spec.md)
- [内部設計](docs/internal-spec/system-design.md)
- [受け入れ・テスト仕様](docs/test-spec/test-requirements.md)
- [traceability](docs/traceability.md)
- [実装状況](docs/implementation-status.md)
- [v0.1 release closure](docs/release-closure.md)
- [実装履歴](docs/implementation-history.md)
- [OpenCV FFmpeg VP9デコーダーとの性能比較](docs/opencv-vp9-decode-comparison.md)

## 現在の主な制限

- decoder seekは将来対応
- 公開frame formatは8-bit。10-bit/P010は将来対応
- GPU画像処理kernelは本ライブラリの責務外
- Windows Intel D3D11とNVIDIA AV1 positive encodeは実機未認定
- macOS、H.264、HEVCは対象外
