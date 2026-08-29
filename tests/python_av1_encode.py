import os
import subprocess
import tempfile
import math

import numpy as np

import mkvcodec


def main() -> None:
    width, height = 64, 48
    with tempfile.TemporaryDirectory() as directory:
        path = os.path.join(directory, "cpu-av1.webm")
        y = np.empty((height, width), np.uint8)
        u = np.full((height // 2, width // 2), 96, np.uint8)
        v = np.full((height // 2, width // 2), 160, np.uint8)
        with mkvcodec.VideoWriter(
            path,
            codec="av1",
            fps=30,
            frame_size=(width, height),
            quality=32,
            queue_size=2,
        ) as writer:
            rows, columns = np.indices((height, width))
            for index in range(30):
                y[:] = (columns * 3 + rows * 2 + index * 7) & 0xFF
                writer.write((y, u, v))
                if index == 14:
                    writer.flush()

        probe = subprocess.run(
            [
                os.environ["FFPROBE_EXECUTABLE"],
                "-v", "error", "-select_streams", "v:0",
                "-show_entries", "stream=codec_name,width,height,nb_read_frames",
                "-count_frames", "-of", "default=nw=1", path,
            ],
            check=True,
            text=True,
            capture_output=True,
        ).stdout
        assert "codec_name=av1" in probe
        assert f"width={width}" in probe and f"height={height}" in probe
        assert "nb_read_frames=30" in probe
        subprocess.run(
            [os.environ["FFMPEG_EXECUTABLE"], "-v", "error", "-i", path,
             "-f", "null", "-"],
            check=True,
        )

        squared_error = 0.0
        sample_count = 0
        pts_values: list[int] = []
        rows, columns = np.indices((height, width))
        with mkvcodec.VideoCapture(path, codec="av1", prefetch=0) as capture:
            while True:
                decoded = capture.read_i420()
                if decoded is None:
                    break
                index = int(round(decoded.pts_ns * 30 / 1_000_000_000))
                expected = ((columns * 3 + rows * 2 + index * 7) & 0xFF).astype(
                    np.float64
                )
                difference = decoded.y.astype(np.float64) - expected
                squared_error += float(np.square(difference).sum())
                sample_count += difference.size
                pts_values.append(decoded.pts_ns)
        assert len(pts_values) == 30, pts_values
        assert pts_values == sorted(pts_values)
        mse = squared_error / sample_count
        psnr = 10.0 * math.log10(255.0 * 255.0 / mse)
        assert psnr >= 28.0

        with mkvcodec.VideoCapture(path, codec="av1", prefetch=4) as capture:
            frames = list(capture)
        assert len(frames) == 30
        assert frames[0].shape == (height, width, 3)

        packed_cases = {
            "bgr": (np.full((height, width, 3), (255, 0, 0), np.uint8),
                    "write_bgr"),
            "rgb": (np.full((height, width, 3), (0, 0, 255), np.uint8),
                    "write_rgb"),
            "bgra": (np.full((height, width, 4), (255, 0, 0, 255), np.uint8),
                     "write_bgra"),
        }
        for name, (image, method_name) in packed_cases.items():
            packed_path = os.path.join(directory, f"av1-{name}.webm")
            with mkvcodec.VideoWriter(
                packed_path, codec="av1", fps=30,
                frame_size=(width, height), queue_size=0
            ) as writer:
                getattr(writer, method_name)(image)
            with mkvcodec.VideoCapture(packed_path, codec="av1") as capture:
                decoded = capture.read_i420()
                assert decoded is not None
                assert capture.read_i420() is None
            assert abs(float(decoded.y.mean()) - 41.0) <= 6

        nv12_path = os.path.join(directory, "av1-nv12.webm")
        nv12_y = np.full((height, width), 41, np.uint8)
        nv12_uv = np.empty((height // 2, width), np.uint8)
        nv12_uv[:, 0::2] = 240
        nv12_uv[:, 1::2] = 110
        with mkvcodec.VideoWriter(
            nv12_path, codec="av1", fps=30,
            frame_size=(width, height), queue_size=0
        ) as writer:
            writer.write_nv12(nv12_y, nv12_uv)
        with mkvcodec.VideoCapture(nv12_path, codec="av1") as capture:
            decoded = capture.read_i420()
        assert decoded is not None
        assert abs(float(decoded.y.mean()) - 41.0) <= 6


if __name__ == "__main__":
    main()
