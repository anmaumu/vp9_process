import os
import tempfile

import numpy as np

import mkvcodec


def expect_value_error(callback) -> None:
    try:
        callback()
    except ValueError:
        return
    raise AssertionError("ValueError was not raised")


def main() -> None:
    with tempfile.TemporaryDirectory() as directory:
        path = os.path.join(directory, "python.webm")
        width, height = 64, 48
        with mkvcodec.VideoWriter(
            path, fps=30, frame_size=(width, height), quality=32
        ) as writer:
            for index in range(30):
                rows, columns = np.indices((height, width))
                y = ((columns * 3 + rows * 2 + index * 7) & 0xFF).astype(np.uint8)
                u = np.full((height // 2, width // 2), 96 + index, np.uint8)
                v = np.full((height // 2, width // 2), 160 - index, np.uint8)
                writer.write((y, u, v))

        with mkvcodec.VideoCapture(path, prefetch=0) as capture:
            frames = []
            while True:
                frame = capture.read_i420()
                if frame is None:
                    break
                frames.append(frame)
        assert len(frames) == 30
        assert frames[0].y.shape == (height, width)
        assert frames[0].u.shape == (height // 2, width // 2)
        assert [frame.pts_ns for frame in frames] == sorted(
            frame.pts_ns for frame in frames
        )
        with mkvcodec.VideoCapture(path, prefetch=4) as capture:
            prefetched = list(capture)
        assert len(prefetched) == 30
        assert prefetched[0].shape == (height, width, 3)
        capture = mkvcodec.VideoCapture(path, prefetch=1)
        assert capture.read_bgr() is not None
        capture.close()
        capture.close()

        expected_blue_y = 41
        packed_cases = {
            "bgr": (np.full((height, width + 4, 3), (255, 0, 0), np.uint8)[:, :width],
                    "write_bgr"),
            "rgb": (np.full((height, width, 3), (0, 0, 255), np.uint8),
                    "write_rgb"),
            "bgra": (np.full((height, width, 4), (255, 0, 0, 255), np.uint8),
                     "write_bgra"),
        }
        for name, (image, method_name) in packed_cases.items():
            packed_path = os.path.join(directory, f"{name}.webm")
            with mkvcodec.VideoWriter(
                packed_path, fps=30, frame_size=(width, height)
            ) as writer:
                if name == "bgr":
                    expect_value_error(
                        lambda: writer.write_bgr(image.astype(np.float32))
                    )
                    expect_value_error(
                        lambda: writer.write_bgr(image[:, :-1])
                    )
                    expect_value_error(
                        lambda: writer.write_bgr(image[::-1])
                    )
                getattr(writer, method_name)(image)
            with mkvcodec.VideoCapture(packed_path) as capture:
                decoded = capture.read_i420()
                assert decoded is not None
                assert capture.read_i420() is None
            assert abs(float(decoded.y.mean()) - expected_blue_y) <= 5

            if name == "bgr":
                with mkvcodec.VideoCapture(packed_path) as capture:
                    bgr = capture.read_bgr()
                    assert bgr is not None
                    assert bgr.shape == (height, width, 3)
                    assert float(bgr[..., 0].mean()) > 240
                    assert float(bgr[..., 2].mean()) < 15
                    assert capture.last_pts_ns == 0
                with mkvcodec.VideoCapture(packed_path) as capture:
                    rgb = capture.read_rgb()
                    assert rgb is not None
                    assert float(rgb[..., 2].mean()) > 240
                    assert float(rgb[..., 0].mean()) < 15
                with mkvcodec.VideoCapture(packed_path) as capture:
                    bgra = capture.read_bgra()
                    assert bgra is not None
                    assert bgra.shape == (height, width, 4)
                    assert np.all(bgra[..., 3] == 255)
                with mkvcodec.VideoCapture(packed_path) as capture:
                    nv12 = capture.read_nv12()
                    assert nv12 is not None
                    assert nv12[0].shape == (height, width)
                    assert nv12[1].shape == (height // 2, width)

        nv12_path = os.path.join(directory, "nv12.webm")
        nv12_y = np.full((height, width), expected_blue_y, np.uint8)
        nv12_uv = np.empty((height // 2, width), np.uint8)
        nv12_uv[:, 0::2] = 240
        nv12_uv[:, 1::2] = 110
        with mkvcodec.VideoWriter(
            nv12_path, fps=30, frame_size=(width, height)
        ) as writer:
            writer.write_nv12(nv12_y, nv12_uv)
        with mkvcodec.VideoCapture(nv12_path) as capture:
            decoded = capture.read_i420()
        assert decoded is not None
        assert abs(float(decoded.y.mean()) - expected_blue_y) <= 5

        long_path = os.path.join(directory, "long.webm")
        y = np.full((height, width), 96, np.uint8)
        u = np.full((height // 2, width // 2), 128, np.uint8)
        v = np.full((height // 2, width // 2), 128, np.uint8)
        with mkvcodec.VideoWriter(
            long_path, fps=30, frame_size=(width, height), quality=40
        ) as writer:
            for index in range(1000):
                y[0, 0] = index & 0xFF
                writer.write((y, u, v))
        with mkvcodec.VideoCapture(long_path) as capture:
            decoded_count = sum(1 for _ in capture)
        assert decoded_count == 1000


if __name__ == "__main__":
    main()
