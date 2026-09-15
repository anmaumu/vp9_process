"""Qualify Intel device-USM NV12-to-RGB dpnp processing on hardware."""

from __future__ import annotations

import gc
import os
import sys
import weakref


def skip(message: str) -> None:
    print(f"Intel dpnp processor test skipped: {message}")
    raise SystemExit(77)


try:
    import dpctl
    import dpnp
except Exception as exception:
    skip(f"dpctl/dpnp unavailable ({exception})")

native_library, extension_dir, package_dir = sys.argv[1:4]
os.environ["MKVC_LIBRARY_PATH"] = native_library
sys.path[:0] = [package_dir, extension_dir]

try:
    import _dlpack
    import mkvcodec
    import mkvcodec.interop.dlpack as dlpack_api
except Exception as exception:
    skip(f"mkvcodec DLPack binding unavailable ({exception})")

dlpack_api.extension = _dlpack


class Owner:
    """Retain the dpnp allocation imported by the native frame lease."""

    def __init__(self, array: object) -> None:
        self.array = array


try:
    queue = dpctl.SyclQueue("level_zero:gpu:0")
    width, height = 64, 48
    nv12 = dpnp.empty(
        (height * 3 // 2, width),
        dtype=dpnp.uint8,
        sycl_queue=queue,
        usm_type="device",
    )
    nv12[:height, :] = 126
    nv12[height:, :] = 128
    queue.wait()
    pointer = int(nv12.__sycl_usm_array_interface__["data"][0])
    owner = Owner(nv12)
    owner_ref = weakref.ref(owner)
    frame = mkvcodec.GpuFrame.import_usm_nv12(
        pointer=pointer,
        context=queue.sycl_context.addressof_ref(),
        queue=queue.addressof_ref(),
        device_id=0,
        frame_size=(width, height),
        pitch=width,
        owner=owner,
        pts_ns=123456789,
        producer_synchronized=True,
    )
    processor = mkvcodec.GpuProcessor(backend="intel")
    if "intel-dpnp" not in processor.adapters:
        skip("optional Intel dpnp adapter was not discovered")
    image = processor.convert(
        frame,
        format="rgb",
        layout="hwc",
        dtype="uint8",
        color_space="bt601",
        color_range="limited",
    )
    frame.close()
    del frame, owner, nv12
    image.wait(5000)
    shared = dpnp.from_dlpack(image, copy=False)
    assert shared.shape == (height, width, 3)
    assert image.pts_ns == 123456789
    assert image.info.copy_path == "gpu_copy"
    image.close()
    del image
    queue.wait()
    values = dpnp.asnumpy(shared)
    assert values.min() == 128 and values.max() == 128, (
        int(values.min()),
        int(values.max()),
        values[0, 0].tolist(),
    )
    del shared
    gc.collect()
    assert owner_ref() is None
except (OSError, RuntimeError, ValueError) as exception:
    skip(str(exception))

print(f"Intel dpnp RGB processor passed on {queue.name}")
