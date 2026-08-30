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

## Frame interopの方針

このlibraryはcodec/containerとCPU/GPU memoryの所有権・同期を担当し、GPU上の
resize、crop、色変換、rotate/flip、letterbox等の画像処理algorithmは内蔵しません。
decode結果をNumPy/OpenCV、CuPy/DLPack、D3D11、VA-API等へexportし、外部libraryで
処理したresourceをencodeへimportする共通のlease/completion APIを目標にします。

- standard NumPyはCPU memoryです。CPU borrowed viewはcopyなしにできますが、viewの
  生存中はdecode bufferを再利用しません。
- GPU decode結果をNumPyで受け取る場合はGPU downloadが必須で、BGR等への変換も
  allocation/copyとしてmetricsへ記録します。
- GPU zero-copy処理はDLPack/native handleで外部libraryへ渡し、処理済みresourceと
  producer event/fenceをencoderへimportします。
- CPU owned NumPy APIと現在のCPU convenience processingは安全な既定機能として残します。
- borrowed CPU decode、同期/非同期encode、固定容量native input poolはC ABI/Pythonへ
  実装済みです。.NETも同じunmanaged poolとcompletion submissionを利用できます。
  OS page-lock付きpool、GPU processed-resource importは未実装です。

## 現在地

CPU VP9のWebM encode/decode、CPU AV1のSVT-AV1 encode/libaom decode、C ABI、
NumPy用Python API、bounded非同期Writer、bounded decode prefetchまで実装済みです。
Intel oneVPLによるVP9/AV1 WebM encode/decodeはC ABIとPython Writer/Captureから
選択でき、Linux Intel GPU実機で検証済みです。NVIDIA NVDEC Captureは
C ABI/Pythonから選択でき、Windows RTX 2060でVP9を検証済みです。
NVENC AV1 WriterはC ABI/Python共通経路へ実装済みで、runtime queryが対応を示すGPU
だけに公開します。RTX 2060での非対応拒否とGPUなしLinuxでの退行は検証済みですが、
AV1 NVENC対応GPUでのpositive encode検証は未完です。NVIDIA VP9 decodeはCUDA pointer
surfaceを公開でき、NVDEC surface→NVENC registered-resource経路と、各NV12 planeを
DLPackへ渡すnative/Python APIを実装済みです。CUDA eventのconsumer-stream dependency、
CuPy実機検証、処理済みCUDA resourceのencoder import、NVIDIA AV1対応GPU検証、
10-bit public frame APIは未完です。
.NET 8 bindingではABI version/capability query、型付きerror、SafeHandleに加え、
`IDisposable`な`MkvVideoWriter`/`MkvVideoCapture`、owned I420 frame、managed arrayの
長時間pinningを避ける`MkvCpuFramePool`/`MkvSubmission`を実装済みです。
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
    # writer.cancel()                          # queueを破棄し待機callerを起床

with mkvcodec.VideoCapture("output.webm", prefetch=4) as capture:
    bgr_frame = capture.read()       # or read_bgr()
    pts_ns = capture.last_pts_ns
```

CPU native I420をcopyせずNumPy viewとして借用する場合は`read_borrowed()`を使います。
返却planeはread-onlyで、frame wrapperを閉じても保持中のplane/sliceがnative leaseを
維持します。同期borrowed encodeは`queue_size=0`、completion付き非同期borrowed
encodeは正数の`queue_size`を使用します。

```python
with mkvcodec.VideoCapture("input.webm", prefetch=0) as capture:
    with capture.read_borrowed() as frame:
        external_result = process_cpu(frame.planes)

with mkvcodec.VideoWriter(
    "output.webm", fps=30, frame_size=(1920, 1080), queue_size=0,
) as writer:
    writer.write_borrowed(external_result, format="i420")

with mkvcodec.VideoWriter(
    "async.webm", fps=30, frame_size=(1920, 1080), queue_size=4,
) as writer:
    submission = writer.submit_borrowed(external_result, format="i420")
    submission.wait()  # ここまでinput ownerを保持し、変更してはいけない
```

長時間のPython/.NET managed-memory pinningを避ける非同期入力には、固定容量の
`CpuFramePool`を使えます。NumPy plane/sliceとsubmissionがnative slot leaseを保持し、
全ownerの解放とencode完了までは同じslotを再取得できません。

```python
pool = mkvcodec.CpuFramePool("i420", (1920, 1080), capacity=4)
with mkvcodec.VideoWriter(
    "pooled.webm", fps=30, frame_size=(1920, 1080), queue_size=4,
) as writer:
    buffer = pool.acquire()
    y, u, v = buffer.planes
    process_into(y, u, v)
    submission = writer.submit_buffer(buffer)
    buffer.close()       # submissionが完了するまではslotをnative側で保持
    submission.wait()
pool.close()
```

C++17では`mkvcodec/mkvcodec.hpp`のheader-only RAII facadeを利用できます。公開binary
境界は引き続きC ABIであり、wrapperはcompiler固有のC++ ABIをDLL境界へ公開しません。

```cpp
mkvcodec::CpuFramePool pool(MKVC_PIXEL_FORMAT_I420, 1920, 1080, 4);
auto buffer = pool.acquire();
auto view = buffer.view();
process_into(view.planes, view.strides);
mkvcodec::Encoder encoder(config);
auto submission = encoder.submit(buffer);
buffer.reset();              // native submissionがslotを保持
submission.wait();
encoder.close();
```

NVIDIA GPU surfaceはY/UV planeごとにDLPack consumerへ渡せます。consumerが
managed tensorを解放するまでnative GPU leaseも保持されます。

外部CUDA pointerは`mkvc_gpu_frame_import_external()`またはC++
`GpuFrame::import_external()`で共通leaseへ取り込めます。producer queryとrelease
callbackは必須の寿命契約で、CUDA-pointer NV12は対応NVENCへ直接submitできます。
.NETは`MkvGpuFrame.ImportExternal()`でmanaged ownerを最終releaseまで保持できます。
Pythonはstable-ABI extension経由の`GpuFrame.import_cuda_pointer()`を利用でき、現時点では
`producer_synchronized=True`を明示した完了済みresourceに限定します。非同期CUDA event
dependencyとDLPack importは今後の実装です。

```python
import cupy as cp

with mkvcodec.VideoCapture(
    "input.webm", codec="vp9", backend="nvidia",
    prefetch=0, require_gpu_resident=True,
) as capture:
    surface = capture.read_surface()
    if surface is not None:
        y = cp.from_dlpack(surface.plane(0))
        uv = cp.from_dlpack(surface.plane(1))
        surface.close()  # y/uvが生存中はnative surfaceも再利用されない
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

GPU-resident経路ではCaptureとWriterへ同じ`MkvGpuFrame` leaseを渡せます。

```csharp
using var capture = new MkvVideoCapture(
    "input.webm", MkvCodecKind.Vp9, MkvBackend.Intel,
    prefetch: 0, requireGpuResident: true);
using var writer = new MkvVideoWriter(
    "output.webm", 1920, 1080, codec: MkvCodecKind.Vp9,
    backend: MkvBackend.Intel, queueSize: 0, requireGpuResident: true);
while (capture.ReadSurface() is { } surface)
{
    using (surface) writer.WriteSurface(surface);
}
```

## Documentation generation

```shell
python tools/docgen.py check
python tools/docgen.py generate
python -m pip install -r requirements-docs.txt
python tools/docgen.py build
```

Intel GPU-resident transcode benchmark:

```bash
python benchmarks/gpu_transcode_benchmark.py \
  --input input.webm --media-output output.webm --codec vp9 \
  --output gpu-result.json
```

Markdown中間生成物は`build/docgen-src`、HTMLは`build/docsite`へ出力されます。
HTML siteにはDoxygenが生成するC/C++ source referenceとXMLも統合されます。
GitHub Actionsはpushとpull requestごとに検証・HTML生成を実行し、artifactを30日保存します。

## Compliance gate

```shell
python tools/compliance_gate.py source
python tools/collect_licenses.py --vcpkg-root path/to/vcpkg \
  --nvcodec-include path/to/nv-codec-headers/include/ffnvcodec \
  --output build/legal/licenses
python tools/compliance_gate.py sbom build/legal/sbom.spdx.json
python tools/compliance_gate.py artifact path/to/package.whl
```

Artifact検査はproject license、third-party notices、SPDX SBOM、bundled依存の
LICENSE/PATENTSが一つでも欠けると失敗します。NVIDIA/Intel GPU driver runtimeは
system dependencyであり、wheel/NuGetへの混入を常に拒否します。
収集するLICENSE/PATENTS原文は固定revisionのSHA-256 allowlistと照合され、差異や
複数候補がある場合はpackage作成を停止します。

Project license確定後のpackage作成例です。`--project-license`は省略できず、空fileも
拒否されます。

```shell
python tools/build_wheel.py --native build/libmkvcodec.so \
  --legal-dir build/legal/licenses --project-license path/to/LICENSE \
  --output-dir dist --platform-tag manylinux_2_28_x86_64
python tools/build_nuget.py --dotnet path/to/dotnet \
  --native build/libmkvcodec.so --legal-dir build/legal/licenses \
  --project-license path/to/LICENSE --output-dir dist --rid linux-x64
```

対応するWindows指定はwheelが`win_amd64`、NuGetが`win-x64`です。生成処理の最後に
compliance gateが自動実行され、native/managed assetの配置も検査されます。

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
