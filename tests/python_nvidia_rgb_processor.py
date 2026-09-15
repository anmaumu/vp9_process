"""Qualify NVDEC-to-RGB CuPy processing and DLPack ownership on hardware."""

from __future__ import annotations

import os
import sys

import numpy as np


def skip(message: str) -> None:
    print(f"NVIDIA RGB processor test skipped: {message}")
    raise SystemExit(77)


try:
    import cupy as cp
except Exception as exception:
    skip(f"CuPy unavailable ({exception})")

native_library, extension_dir, package_dir, video = sys.argv[1:5]
os.environ["MKVC_LIBRARY_PATH"] = native_library
sys.path.insert(0, package_dir)
sys.path.insert(0, extension_dir)

try:
    import _dlpack
    import mkvcodec
    import mkvcodec.interop.dlpack as dlpack_api
except Exception as exception:
    skip(f"mkvcodec DLPack binding unavailable ({exception})")

dlpack_api.extension = _dlpack


def reference_rgb(frame: object) -> np.ndarray:
    """Convert one CPU I420 frame with the adapter's BT.601 limited matrix."""
    y = frame.y.astype(np.float32)
    u = np.repeat(np.repeat(frame.u, 2, axis=0), 2, axis=1).astype(np.float32) - 128.0
    v = np.repeat(np.repeat(frame.v, 2, axis=0), 2, axis=1).astype(np.float32) - 128.0
    y = (y - 16.0) * (255.0 / 219.0)
    scale = 255.0 / 224.0
    kr, kb = 0.2990, 0.1140
    kg = 1.0 - kr - kb
    output = np.empty((frame.y.shape[0], frame.y.shape[1], 3), dtype=np.uint8)
    output[..., 0] = np.clip(y + scale * 2.0 * (1.0 - kr) * v, 0, 255) + 0.5
    output[..., 1] = np.clip(
        y
        - scale * 2.0 * kb * (1.0 - kb) / kg * u
        - scale * 2.0 * kr * (1.0 - kr) / kg * v,
        0,
        255,
    ) + 0.5
    output[..., 2] = np.clip(y + scale * 2.0 * (1.0 - kb) * u, 0, 255) + 0.5
    return output


try:
    processor = mkvcodec.GpuProcessor()
    if "nvidia-cupy" not in processor.adapters:
        skip("optional NVIDIA CuPy adapter was not discovered")
    with mkvcodec.VideoCapture(video, backend="cpu", prefetch=0) as cpu_capture:
        cpu_frame = cpu_capture.read_i420()
    if cpu_frame is None:
        raise AssertionError("CPU reference decoder returned no frame")
    reference = reference_rgb(cpu_frame)

    converted = 0
    with mkvcodec.VideoCapture(
        video, backend="nvidia", prefetch=0, require_gpu_resident=True
    ) as capture:
        while converted < 16:
            image = capture.read_gpu(
                processor,
                format="rgb",
                layout="hwc",
                dtype="uint8",
                color_space="bt601",
                color_range="limited",
            )
            if image is None:
                break
            with image:
                tensor = cp.from_dlpack(image)
                assert tensor.shape == reference.shape
                assert tensor.dtype == cp.uint8
                if converted == 0:
                    observed = cp.asnumpy(tensor)
                    difference = np.abs(
                        observed.astype(np.int16) - reference.astype(np.int16)
                    )
                    assert float(difference.mean()) <= 0.05
                    assert int(difference.max()) <= 2
            converted += 1
        assert capture.metrics.copy_path == "zero_copy"
    assert converted > 0
    cp.cuda.get_current_stream().synchronize()
except (OSError, RuntimeError, ValueError) as exception:
    skip(str(exception))

print(f"NVIDIA CuPy RGB processor passed for {converted} frames")
