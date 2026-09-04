import sys

import mkvcodec


def main(path: str) -> None:
    capture = mkvcodec.VideoCapture(
        path, codec="vp9", backend="intel", prefetch=0
    )
    surface = capture.read_surface()
    assert surface is not None
    surface.wait(5000)
    descriptor = surface.descriptor
    native = surface.native_handle
    assert descriptor["backend"] == 3
    assert descriptor["width"] > 0 and descriptor["height"] > 0
    assert descriptor["generation"] > 0
    assert native["borrowed"] is True
    assert native["generation"] == descriptor["generation"]
    assert native["handles"][0] != 0
    assert surface.interop.backend == "intel"
    assert surface.interop.memory_type in ("va_surface", "d3d11_texture")
    capture.close()
    assert capture.metrics.copy_path == "zero_copy"
    assert surface.descriptor["generation"] == descriptor["generation"]
    surface.close()

    with mkvcodec.VideoCapture(
        path, codec="vp9", backend="auto", require_gpu_resident=True
    ) as automatic:
        selected = automatic.read_surface()
        assert selected is not None
        assert automatic.backend in ("intel", "nvidia")
        assert selected.interop.backend == automatic.backend
        selected.close()


if __name__ == "__main__":
    main(sys.argv[1])
