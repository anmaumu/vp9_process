import os
import sys
import tempfile

import numpy as np

import mkvcodec


def main() -> None:
    width, height = 320, 240
    with tempfile.TemporaryDirectory() as directory:
        for codec in ("vp9", "av1"):
            extension = "mkv" if codec == "av1" else "webm"
            path = os.path.join(directory, f"intel-{codec}.{extension}")
            try:
                writer = mkvcodec.VideoWriter(
                    path,
                    codec=codec,
                    backend="intel",
                    fps=30,
                    frame_size=(width, height),
                    quality=32,
                    queue_size=2,
                )
            except ValueError as exc:
                print(f"Intel encoder unavailable: {exc}")
                return 1 if os.environ.get("MKVC_REQUIRE_INTEL_GPU") else 77
            rows, columns = np.indices((height, width))
            image = np.empty((height, width, 3), np.uint8)
            i420_y = np.empty((height, width), np.uint8)
            i420_u = np.full((height // 2, width // 2), 96, np.uint8)
            i420_v = np.full((height // 2, width // 2), 160, np.uint8)
            nv12_uv = np.empty((height // 2, width), np.uint8)
            nv12_uv[:, 0::2] = 96
            nv12_uv[:, 1::2] = 160
            with writer:
                for index in range(30):
                    image[..., 0] = (columns + index * 3) & 0xFF
                    image[..., 1] = (rows + index * 5) & 0xFF
                    image[..., 2] = (columns + rows + index * 7) & 0xFF
                    mode = index % 5
                    if mode == 0:
                        writer.write(image)
                    elif mode == 1:
                        writer.write_rgb(image[..., ::-1].copy())
                    elif mode == 2:
                        bgra = np.empty((height, width, 4), np.uint8)
                        bgra[..., :3] = image
                        bgra[..., 3] = 255
                        writer.write_bgra(bgra)
                    elif mode == 3:
                        i420_y[:] = (columns + rows + index * 4) & 0xFF
                        writer.write_i420(i420_y, i420_u, i420_v)
                    else:
                        i420_y[:] = (columns * 2 + rows + index * 3) & 0xFF
                        writer.write_nv12(i420_y, nv12_uv)
                    if index == 14:
                        writer.flush()
            assert writer.metrics.accepted_frames == 30
            assert writer.metrics.completed_frames == 30
            assert writer.metrics.hardware_pending_peak == 4
            assert writer.metrics.copy_path == "cpu"

            capture = mkvcodec.VideoCapture(
                path, codec=codec, backend="intel", prefetch=0
            )
            surface = capture.read_surface()
            assert surface is not None
            surface.wait(5000)
            descriptor = surface.descriptor
            native_handle = surface.native_handle
            assert descriptor["backend"] == 3
            assert descriptor["width"] == width
            assert descriptor["height"] == height
            assert descriptor["generation"] > 0
            assert native_handle["borrowed"] is True
            assert native_handle["handles"][0] != 0
            capture.close()
            assert capture.metrics.copy_path == "zero_copy"
            assert surface.descriptor["generation"] == descriptor["generation"]
            surface.close()

            for backend, prefetch in (("intel", 0), ("intel", 4), ("cpu", 4)):
                if backend == "cpu" and os.environ.get(
                    f"MKVC_TEST_CPU_{codec.upper()}", "1"
                ) == "0":
                    print(f"CPU {codec} reference decoder disabled in this build")
                    continue
                with mkvcodec.VideoCapture(
                    path, codec=codec, backend=backend, prefetch=prefetch
                ) as capture:
                    frames = list(capture)
                assert capture.metrics.accepted_frames == 30
                assert capture.metrics.completed_frames == 30
                assert capture.metrics.hardware_pending_peak == (
                    4 if backend == "intel" else 0
                )
                assert capture.metrics.copy_path == "cpu"
                assert len(frames) == 30
                assert frames[0].shape == (height, width, 3)
                assert frames[-1].shape == (height, width, 3)
                assert float(frames[0].std()) > 10
    print("Intel VP9/AV1 WebM encode and sync/prefetch decode passed")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"Intel round-trip failed: {exc}", file=sys.stderr)
        raise
