import sys
from pathlib import Path

import mkvcodec
import numpy as np


def main(input_path: str, output_path: str) -> None:
    output = Path(output_path)
    output.unlink(missing_ok=True)
    count = 0
    with mkvcodec.VideoCapture(
        input_path, codec="vp9", backend="intel", prefetch=0,
        require_gpu_resident=True,
    ) as capture:
        first = capture.read_surface()
        assert first is not None
        try:
            capture.read_i420()
            raise AssertionError("strict capture accepted a CPU read")
        except RuntimeError as exc:
            assert "require_gpu_resident" in str(exc)
        descriptor = first.descriptor
        with mkvcodec.VideoWriter(
            output, codec="vp9", backend="intel", fps=30,
            frame_size=(descriptor["width"], descriptor["height"]),
            queue_size=0,
            require_gpu_resident=True,
        ) as writer:
            try:
                writer.write_bgr(np.zeros(
                    (descriptor["height"], descriptor["width"], 3), np.uint8
                ))
                raise AssertionError("strict writer accepted a CPU frame")
            except RuntimeError as exc:
                assert "require_gpu_resident" in str(exc)
            surface = first
            while surface is not None:
                writer.write_surface(surface)
                surface.close()
                count += 1
                surface = capture.read_surface()
            assert writer.metrics.copy_path == "zero_copy"
    assert count > 0
    assert output.stat().st_size > 0

    decoded = 0
    with mkvcodec.VideoCapture(
        output, codec="vp9", backend="intel", prefetch=0
    ) as verification:
        while True:
            surface = verification.read_surface()
            if surface is None:
                break
            decoded += 1
            surface.close()
    assert decoded == count, (decoded, count)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
