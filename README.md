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

最初の実装段階です。C ABI、CMake、backend capability APIの骨格までを提供し、
codec backendはまだ登録していません。利用可能と報告される機能は、実際に初期化に
成功したbackendだけに限定する設計です。

## Build

```shell
cmake --preset default
cmake --build --preset default
ctest --preset default
```

