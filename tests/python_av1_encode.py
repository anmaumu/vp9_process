import os
import subprocess
import tempfile

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


if __name__ == "__main__":
    main()
