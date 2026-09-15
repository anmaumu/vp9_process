"""Exercise the optional CuPy adapter without requiring a CUDA device."""

from __future__ import annotations

import os
import sys
import types

native_library, package_dir = sys.argv[1:3]
os.environ["MKVC_LIBRARY_PATH"] = native_library
sys.path.insert(0, package_dir)


class FakeArray:
    def __init__(self, shape: tuple[int, ...], strides: tuple[int, ...]) -> None:
        self.shape = shape
        self.strides = strides

    def __dlpack_device__(self) -> tuple[int, int]:
        return 2, 7

    def __dlpack__(self, **arguments: object) -> object:
        return "fake-capsule", arguments


class FakeStream:
    pass


class FakeEvent:
    def __init__(self, *, disable_timing: bool) -> None:
        assert disable_timing
        self.recorded = False
        self.synchronized = 0

    def record(self, stream: object) -> None:
        assert isinstance(stream, FakeStream)
        self.recorded = True

    def query(self) -> bool:
        return self.recorded

    def synchronize(self) -> None:
        self.synchronized += 1


class FakeKernel:
    calls: list[tuple[object, ...]] = []

    def __init__(self, source: str, name: str) -> None:
        assert "mkvc_nv12_to_packed_u8" in source
        assert name == "mkvc_nv12_to_packed_u8"

    def __call__(
        self,
        grid: object,
        block: object,
        arguments: object,
        *,
        stream: object,
    ) -> None:
        self.calls.append((grid, block, arguments, stream))


fake_stream = FakeStream()
fake_cupy = types.ModuleType("cupy")
fake_cupy.uint8 = "uint8"
fake_cupy.RawKernel = FakeKernel
fake_cupy.empty = lambda shape, dtype: FakeArray(shape, (shape[-1], 1))
fake_cupy.from_dlpack = lambda plane: FakeArray(
    (48, 64) if plane.index == 0 else (24, 64), (80, 1)
)
fake_cupy.cuda = types.SimpleNamespace(
    get_current_stream=lambda: fake_stream,
    Event=FakeEvent,
)
sys.modules["cupy"] = fake_cupy

import mkvcodec  # noqa: E402


class Interop:
    backend = "nvidia"
    dlpack_export = True


class Plane:
    def __init__(self, index: int) -> None:
        self.index = index


class Retained:
    def __init__(self) -> None:
        self.closed = 0

    def close(self) -> None:
        self.closed += 1


class Source:
    interop = Interop()
    descriptor = {
        "memory_type": 3,
        "pixel_format": 2,
        "plane_count": 2,
        "device_id": 7,
        "width": 64,
        "height": 48,
        "pts_ns": 1234,
    }

    def __init__(self) -> None:
        self.retained = Retained()

    def plane(self, index: int) -> Plane:
        return Plane(index)

    def retain(self) -> Retained:
        return self.retained


processor = mkvcodec.GpuProcessor()
assert processor.adapters == ("nvidia-cupy",)
source = Source()
image = processor.convert(source, format="bgra", layout="chw", dtype="uint8")
assert image.info.shape == (4, 48, 64)
assert image.shape == (4, 48, 64)
assert image.pts_ns == 1234
assert image.info.copy_path == "gpu_copy"
assert image.__dlpack_device__() == (2, 7)
assert image.__dlpack__(stream=9) == ("fake-capsule", {"stream": 9})
assert len(FakeKernel.calls) == 1
image.wait(1)
image.close()
image.close()
assert source.retained.closed == 1
print("optional CuPy GPU processor contract passed")
