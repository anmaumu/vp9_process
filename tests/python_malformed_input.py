"""Exercise deterministic malformed-container mutations through the public API."""

from __future__ import annotations

import pathlib
import random
import tempfile

import numpy as np

import mkvcodec


EXPECTED_FAILURES = (ValueError, RuntimeError, OSError)


def exercise(path: pathlib.Path, maximum_frames: int) -> bool:
    """Probe and decode one mutation without permitting unbounded output.

    Parameters
    ----------
    path:
        Mutated WebM file.
    maximum_frames:
        Largest output count permitted for the original fixture.

    Returns
    -------
    bool
        True when the mutation remains a structurally usable media file.
    """
    try:
        info = mkvcodec.probe_video(path)
        assert 0 < info.width <= 32768
        assert 0 < info.height <= 32768
        assert info.width * info.height <= 268435456
        count = 0
        with mkvcodec.VideoCapture(path, prefetch=0) as capture:
            while capture.read_i420() is not None:
                count += 1
                assert count <= maximum_frames
        return True
    except EXPECTED_FAILURES:
        return False


def main() -> None:
    """Create a valid fixture and run fixed plus seeded mutation cases."""
    width, height, frame_count = 160, 128, 8
    with tempfile.TemporaryDirectory() as temporary:
        root = pathlib.Path(temporary)
        valid = root / "valid.webm"
        y = np.empty((height, width), dtype=np.uint8)
        u = np.full((height // 2, width // 2), 96, dtype=np.uint8)
        v = np.full((height // 2, width // 2), 160, dtype=np.uint8)
        rows, columns = np.indices((height, width))
        with mkvcodec.VideoWriter(valid, frame_size=(width, height), fps=30) as writer:
            for index in range(frame_count):
                y[:] = (columns * 3 + rows * 2 + index * 11) & 0xFF
                writer.write((y, u, v))
        original = valid.read_bytes()
        assert exercise(valid, frame_count)

        rejected = 0
        fixed = [
            b"", b"\x00", b"not an EBML container", b"\xff" * 128,
            *[original[:size] for size in (1, 4, 16, 32, 64, 128)],
        ]
        for index, payload in enumerate(fixed):
            path = root / f"fixed-{index}.webm"
            path.write_bytes(payload)
            rejected += not exercise(path, frame_count)
        assert rejected == len(fixed)

        generator = random.Random(0x4D4B5643)
        for index in range(64):
            payload = bytearray(original)
            if index < 16:
                payload = payload[:generator.randrange(1, len(payload))]
            else:
                for _ in range(1 + index % 4):
                    position = generator.randrange(min(len(payload), 4096))
                    payload[position] ^= 1 << generator.randrange(8)
            path = root / f"mutation-{index}.webm"
            path.write_bytes(payload)
            exercise(path, frame_count)


if __name__ == "__main__":
    main()
