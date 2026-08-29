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

        with mkvcodec.VideoCapture(path) as capture:
            frames = list(capture)
        assert len(frames) == 30
        assert frames[0].y.shape == (height, width)
        assert frames[0].u.shape == (height // 2, width // 2)
        assert [frame.pts_ns for frame in frames] == sorted(
            frame.pts_ns for frame in frames
        )

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


if __name__ == "__main__":
    main()
