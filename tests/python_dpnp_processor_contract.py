"""Exercise the optional dpnp adapter without requiring an Intel GPU."""

from __future__ import annotations

import os
import sys
import types

import numpy as np

native_library, package_dir = sys.argv[1:3]
os.environ["MKVC_LIBRARY_PATH"] = native_library
sys.path.insert(0, package_dir)


class FakeQueue:
    def __init__(self) -> None:
        self.waits = 0

    def wait(self) -> None:
        self.waits += 1


class FakeArray:
    """Small NumPy-backed subset of the dpnp array contract."""

    def __init__(self, value: object, queue: FakeQueue) -> None:
        self.value = np.asarray(value)
        self.sycl_queue = queue

    def __getitem__(self, key: object) -> "FakeArray":
        return FakeArray(self.value[key], self.sycl_queue)

    def __setitem__(self, key: object, value: object) -> None:
        self.value[key] = value.value if isinstance(value, FakeArray) else value

    def astype(self, dtype: object) -> "FakeArray":
        return FakeArray(self.value.astype(dtype), self.sycl_queue)

    def _binary(self, other: object, operation: object) -> "FakeArray":
        value = other.value if isinstance(other, FakeArray) else other
        return FakeArray(operation(self.value, value), self.sycl_queue)

    def __add__(self, other: object) -> "FakeArray":
        return self._binary(other, np.add)

    __radd__ = __add__

    def __sub__(self, other: object) -> "FakeArray":
        return self._binary(other, np.subtract)

    def __mul__(self, other: object) -> "FakeArray":
        return self._binary(other, np.multiply)

    __rmul__ = __mul__

    def __dlpack_device__(self) -> tuple[int, int]:
        return 14, 5

    def __dlpack__(self, **arguments: object) -> object:
        return "fake-intel-capsule", arguments


queue = FakeQueue()
imports: list[object] = []


def from_dlpack(plane: object, *, copy: bool) -> FakeArray:
    assert copy is False
    imports.append(plane)
    return plane.array


fake_dpnp = types.ModuleType("dpnp")
fake_dpnp.uint8 = np.uint8
fake_dpnp.int32 = np.int32
fake_dpnp.float32 = np.float32
fake_dpnp.from_dlpack = from_dlpack
fake_dpnp.repeat = lambda value, count, axis: FakeArray(
    np.repeat(value.value, count, axis=axis), value.sycl_queue
)
fake_dpnp.clip = lambda value, low, high: FakeArray(
    np.clip(value.value, low, high), value.sycl_queue
)
fake_dpnp.empty = lambda shape, dtype, sycl_queue, usm_type: FakeArray(
    np.empty(shape, dtype=dtype), sycl_queue
)
sys.modules["dpnp"] = fake_dpnp

import mkvcodec  # noqa: E402
from mkvcodec.interop.dpnp_processor import IntelDpnpProcessorAdapter  # noqa: E402


class Interop:
    backend = "intel"
    dlpack_export = True


class Plane:
    def __init__(self, value: np.ndarray) -> None:
        self.array = FakeArray(value, queue)


class Retained:
    def __init__(self) -> None:
        self.closed = 0

    def close(self) -> None:
        self.closed += 1


class Source:
    interop = Interop()
    descriptor = {
        "memory_type": 5,
        "pixel_format": 2,
        "plane_count": 2,
        "device_id": 5,
        "width": 64,
        "height": 48,
        "pts_ns": 9876,
    }

    def __init__(self) -> None:
        self.retained = Retained()
        self.planes = (
            Plane(np.full((48, 64), 126, dtype=np.uint8)),
            Plane(np.full((24, 64), 128, dtype=np.uint8)),
        )

    def plane(self, index: int) -> Plane:
        return self.planes[index]

    def retain(self) -> Retained:
        return self.retained


source = Source()
processor = mkvcodec.GpuProcessor(adapters=(IntelDpnpProcessorAdapter(),))
image = processor.convert(source, format="rgb", layout="hwc", dtype="uint8")
assert image.backend == "intel" and image.shape == (48, 64, 3)
assert image.pts_ns == 9876
assert image.info.color_space == "bt601" and image.info.color_range == "limited"
assert image.__dlpack_device__() == (14, 5)
assert image.__dlpack__(stream=11) == ("fake-intel-capsule", {"stream": 11})
assert len(imports) == 2
assert np.all(image._provider.value[..., 0] == 128)
image.wait(1000)
image.close()
image.close()
assert queue.waits == 2
assert source.retained.closed == 1
print("optional dpnp GPU processor contract passed")
