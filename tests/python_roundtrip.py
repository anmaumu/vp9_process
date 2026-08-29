import os
import tempfile

import numpy as np

import mkvcodec


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


if __name__ == "__main__":
    main()

