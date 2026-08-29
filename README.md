# vp9_process / mkvcodec

VP9/AV1専用のWebM/Matroska encode・decodeライブラリです。C ABIを中核にし、
PythonからOpenCVに近い感覚で利用できるAPIと、将来の.NET bindingを提供します。

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
GPU backendと10-bit public frame APIは未実装です。
利用可能と報告される機能は、実装済みbackendだけに限定します。
Intel oneVPLはhardware sessionとVP9/AV1 encode/decode Queryまで実機検証済みですが、
frame pipeline接続前のため公開backend capabilityにはまだ含めません。

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
