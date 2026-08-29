# Implementation plan

仕様IDは外部仕様 `EXT`、受入条件 `AC`、内部仕様 `INT`、テスト `TEST` の順で
追跡する。詳細仕様書の移管後もこの規則を維持する。

## Phase 0: foundation

- CMakeによるWindows/Linux shared library build
- 例外を境界外へ出さないC ABI
- fixed-width型、`struct_size`、ABI version
- backend capability query
- CTest smoke test

## Phase 1: CPU VP9 vertical slice

- libvpx decode/encode
- libwebm demux/mux
- native capture/writer handles
- synchronous C ABI
- Python NumPy copy API
- WebM VP9 round-trip integration test

## Phase 2: asynchronous pipeline

- bounded queueとbuffer pool
- blocking write / non-blocking try-write
- ordered drain、flush、finalize
- prefetch decode

## Phase 3: hardware backends

- Intel oneVPL VP9/AV1 on `linux-machine`
- NVIDIA NVDEC VP9/AV1
- NVIDIA NVENC AV1（対応GPUが必要）
- CPU fallbackと明示backend選択

## Phase 4: language bindings and distribution

- Python wheel
- .NET P/Invoke / NuGet
- SBOM、THIRD_PARTY_NOTICES、license gate

