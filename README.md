# vp9_process / mkvcodec

VP9/AV1専用のWebM/Matroska encode・decodeライブラリです。C ABIを中核にし、
PythonからOpenCVに近い感覚で利用できるAPIと.NET bindingを提供します。

## 対象

- Windows x64 / Linux x86_64
- CPU: libvpx、libaom、SVT-AV1
- NVIDIA: NVDEC VP9/AV1、NVENC AV1
- Intel: oneVPL VP9/AV1 encode/decode
- Container: libwebmによるWebM/Matroska

H.264とHEVCは対象外です。

## 現在地

CPU VP9のWebM encode/decode、CPU AV1のSVT-AV1 encode/libaom decode、C ABI、
NumPy用Python API、bounded非同期Writer、bounded decode prefetchまで実装済みです。
Intel oneVPLによるVP9/AV1 WebM encode/decodeはC ABIとPython Writer/Captureから
選択でき、Linux Intel GPU実機で検証済みです。NVIDIA NVDEC Captureは
C ABI/Pythonから選択でき、Windows RTX 2060でVP9を検証済みです。
NVENC AV1 WriterはC ABI/Python共通経路へ実装済みで、runtime queryが対応を示すGPU
だけに公開します。RTX 2060での非対応拒否とGPUなしLinuxでの退行は検証済みですが、
AV1 NVENC対応GPUでのpositive encode検証は未完です。NVIDIA AV1 decodeの対応GPU検証、
GPU surface zero-copy、10-bit public frame APIも未完です。
.NET 8 bindingではABI version/capability query、型付きerror、SafeHandleに加え、
`IDisposable`な`MkvVideoWriter`/`MkvVideoCapture`とowned I420 frameを実装済みです。
利用可能と報告される機能は、実装済みbackendだけに限定します。
Intel capabilityはruntime Queryに成功したcodec/directionだけを公開します。
Windows NVIDIA VP9 decodeを実GPU検証済みです。Windows Intel GPU検証は未完です。

## Build

```shell
cmake --preset default
cmake --build --preset default
ctest --preset default
```

## Python I420 example

```python
import mkvcodec

with mkvcodec.VideoWriter(
    "output.webm",
    fps=30,
    frame_size=(1920, 1080),
    quality=32,
) as writer:
    writer.write(bgr_ndarray)                 # OpenCV-style BGR
    writer.write((y_plane, u_plane, v_plane)) # I420
    accepted = writer.try_write(bgr_ndarray)  # queue満杯ならFalse

with mkvcodec.VideoCapture("output.webm", prefetch=4) as capture:
    bgr_frame = capture.read()       # or read_bgr()
    pts_ns = capture.last_pts_ns
```

開発時はnative libraryの場所を`MKVC_LIBRARY_PATH`で指定できます。wheelへのnative
library同梱はdistribution phaseで追加します。

CPU WriterはBGR、RGB、BGRA、I420、NV12を受け付け、libyuvでI420へ変換します。
RGB系やNV12を明示するときは`write_rgb`、`write_bgra`、`write_nv12`を使用します。
Captureの既定`read()`とiteratorはBGR ndarrayを返します。`read_i420`、
`read_rgb`、`read_bgra`、`read_nv12`も選択できます。
`prefetch=0`は同期decode、正数はnative側の固定容量先読みqueueを使用します。
Writerの`queue_size=0`は同期encode、正数（Python既定8）は入力をdeep copyして
native workerへ渡します。通常の`write`はqueue空きを待ち、`try_write`は待機しません。
CPU AV1 writer/captureは`codec="av1"`で選択します。Writer入力とCapture出力は
VP9と同じBGR/RGB/BGRA/I420/NV12を使用できます。
Intel Writerは`backend="intel"`で選択でき、VP9/AV1と同じ5種類の8-bit入力を
内部でNV12へ変換します。Intel Captureも`backend="intel"`で選択でき、同期readと
正数`prefetch`のbounded先読みを利用できます。返却フレームは所有されたI420を経由し、
CPU Captureと同じBGR/RGB/BGRA/I420/NV12出力APIを使用できます。

## .NET ABI smoke

```shell
cmake -S . -B build -DMKVC_BUILD_DOTNET_TESTS=ON
cmake --build build
ctest --test-dir build -R mkvc_dotnet --output-on-failure
```

`MKVC_LIBRARY_PATH`はCTestがbuild済みnative libraryへ設定します。高水準の
NuGet packagingは後続段階です。

## Documentation generation

```shell
python tools/docgen.py check
python tools/docgen.py generate
python -m pip install -r requirements-docs.txt
python tools/docgen.py build
```

Markdown中間生成物は`build/docgen-src`、HTMLは`build/docsite`へ出力されます。
HTML siteにはDoxygenが生成するC/C++ source referenceとXMLも統合されます。
GitHub Actionsはpushとpull requestごとに検証・HTML生成を実行し、artifactを30日保存します。

## Performance baseline

公開Python APIを通る再現可能なJSON benchmarkを提供します。

```shell
python benchmarks/pipeline_benchmark.py --backend intel --codec av1 \
  --width 1920 --height 1080 --frames 300 --fps 60 \
  --output benchmark-results/intel-av1-1080p60.json
```

測定項目と比較条件は`docs/benchmarking.md`を参照してください。
Writer/Captureの`metrics` propertyから、frame数、queue wait、backend時間、
queue peak、Intel pending peak、実copy pathの累積snapshotも取得できます。
