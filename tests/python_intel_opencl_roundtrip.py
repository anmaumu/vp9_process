"""Decode -> external OpenCL inversion -> VA shared import -> VP9 encode."""
import gc
import os
import sys
import tempfile
import weakref
import faulthandler

native_library, extension_dir, package_dir, fixture = sys.argv[1:5]
os.environ["MKVC_LIBRARY_PATH"] = native_library
sys.path[:0] = [package_dir, extension_dir]
import numpy as np
import _dlpack
import mkvcodec
import mkvcodec._api as api
from intel_va_opencl_support import Unsupported, VaOwner, invert_luma
api._dlpack = _dlpack
original_check = api.native.check


def checked(result):
    if result == 3:
        raise Unsupported(api.native.lib.mkvc_get_last_error().decode())
    original_check(result)


api.native.check = checked


def main():
    frames = int(os.environ.get("MKVC_OPENCL_TEST_FRAMES", "32"))
    assert 1 <= frames <= 10000
    with mkvcodec.VideoCapture(fixture, backend="cpu", prefetch=0) as reference:
        reference_y = reference.read_i420().y.astype(np.float32)
    with mkvcodec.VideoCapture(fixture, backend="intel", prefetch=0,
                              require_gpu_resident=True) as capture:
        source = capture.read_surface()
        source.wait(5000)
        desc = source.descriptor
        width, height = desc["width"], desc["height"]
    source_ref = weakref.ref(source)
    with tempfile.TemporaryDirectory() as directory:
        path = os.path.join(directory, "opencl-inverted.webm")
        with mkvcodec.VideoWriter(path, backend="intel", fps=30,
                                 frame_size=(width, height), queue_size=0,
                                 require_gpu_resident=True) as writer:
            for index in range(frames):
                owner = VaOwner(source, width, height)
                invert_luma(source, owner, width, height)
                imported = mkvcodec.GpuFrame.import_va_surface(
                    display=owner.display, surface_id=owner.surface.value,
                    device_id=desc["device_id"], frame_size=(width, height),
                    pts_ns=index * 33333333, owner=owner, producer_synchronized=True)
                del owner
                assert imported.native_handle["handles"][0] == source.native_handle["handles"][0]
                assert imported.native_handle["handles"][1] != source.native_handle["handles"][1]
                writer.write_surface(imported)
                imported.close()
                del imported
                gc.collect()
                # Display anchor plus runtime-referenced imports; no unbounded owners.
                assert 1 <= VaOwner.live <= 65
            del source
            gc.collect()
            assert source_ref() is not None
            assert writer.metrics.copy_path == "zero_copy"  # Library boundary only.
        gc.collect()
        assert source_ref() is None
        assert VaOwner.live == 0 and VaOwner.released == frames
        assert VaOwner.peak <= 65
        count, previous_pts = 0, -1
        with mkvcodec.VideoCapture(path, backend="cpu", prefetch=0) as capture:
            while (frame := capture.read_i420()) is not None:
                difference = frame.y.astype(np.float32) - (255 - reference_y)
                assert float(np.mean(difference * difference)) < 205.63  # Y PSNR > 25 dB.
                assert frame.pts_ns > previous_pts
                previous_pts = frame.pts_ns
                count += 1
        assert count == frames
    print(f"External OpenCL inversion roundtrip: {frames} frames; owner peak={VaOwner.peak}; "
          "no host pixel transfer in producer; driver-internal copy path unqualified")


if __name__ == "__main__":
    faulthandler.enable()
    try:
        main()
    except Unsupported as error:
        print(error)
        sys.exit(1 if os.environ.get("MKVC_REQUIRE_INTEL_EXTERNAL_IMPORT") else 77)
