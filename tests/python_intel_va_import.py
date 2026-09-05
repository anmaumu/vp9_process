"""Linux native VA import through Python, including owner-first GC ordering."""
import gc
import os
import sys
import tempfile
import weakref

native_library, extension_dir, package_dir, fixture = sys.argv[1:5]
os.environ["MKVC_LIBRARY_PATH"] = native_library
sys.path[:0] = [package_dir, extension_dir]
import _dlpack
import mkvcodec
import mkvcodec._api as api
import mkvcodec._gpu as gpu_api
gpu_api._dlpack = _dlpack

# Preserve the native result to distinguish unsupported hardware from real bugs.
original_check = api.native.check
class Unsupported(RuntimeError):
    pass
def checked(result):
    if result == 3:
        raise Unsupported(api.native.lib.mkvc_get_last_error().decode())
    original_check(result)
api.native.check = checked

def main():
    with mkvcodec.VideoCapture(fixture, backend="intel", prefetch=0,
                              require_gpu_resident=True) as capture:
        source = capture.read_surface()
        assert source is not None
        source.wait(5000)
        descriptor, native = source.descriptor, source.native_handle
        width, height = descriptor["width"], descriptor["height"]
        imported = mkvcodec.GpuFrame.import_va_surface(
            display=native["handles"][0], surface_id=native["handles"][1],
            device_id=descriptor["device_id"], frame_size=(width, height), owner=source)
        imported.wait(5000)
        owner_ref = weakref.ref(source)
        del source
    gc.collect()
    assert owner_ref() is not None
    with tempfile.TemporaryDirectory() as directory:
        path = os.path.join(directory, "external-va.webm")
        with mkvcodec.VideoWriter(path, backend="intel", fps=30,
                                 frame_size=(width, height), queue_size=0,
                                 require_gpu_resident=True) as writer:
            for _ in range(4):
                writer.write_surface(imported)
            imported.close()
            del imported
            gc.collect()
            assert owner_ref() is not None  # Encoder holds the display anchor.
            assert writer.metrics.copy_path == "zero_copy"
        gc.collect()
        assert owner_ref() is None
        with mkvcodec.VideoCapture(path, backend="cpu", prefetch=0) as decoded:
            frames = list(decoded)
        assert len(frames) == 4
        assert all(frame.shape == (height, width, 3) for frame in frames)
        assert all(float(frame.std()) > 10 for frame in frames)
    print("Python native VA synchronization, encode and owner lifetime passed")

if __name__ == "__main__":
    try:
        main()
    except Unsupported as error:
        print(error)
        sys.exit(1 if os.environ.get("MKVC_REQUIRE_INTEL_EXTERNAL_IMPORT") else 77)
