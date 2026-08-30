import ctypes as ct
import gc
import os
import sys


def skip(message: str) -> None:
    print(f"NVIDIA CuPy DLPack test skipped: {message}")
    raise SystemExit(77)


try:
    import cupy as cp
except Exception as exc:
    skip(f"CuPy unavailable ({exc})")

native_library, extension_dir, package_dir = sys.argv[1:4]
os.environ["MKVC_LIBRARY_PATH"] = native_library
sys.path.insert(0, package_dir)
sys.path.insert(0, extension_dir)

import _dlpack
import mkvcodec
import mkvcodec._api as api

api._dlpack = _dlpack


def current_context() -> int:
    try:
        driver = ct.WinDLL("nvcuda.dll") if os.name == "nt" else ct.CDLL("libcuda.so.1")
    except OSError as exc:
        skip(f"CUDA driver unavailable ({exc})")
    get_current = driver.cuCtxGetCurrent
    get_current.argtypes = [ct.POINTER(ct.c_void_p)]
    get_current.restype = ct.c_int
    context = ct.c_void_p()
    if get_current(ct.byref(context)) != 0 or not context.value:
        skip("CuPy did not establish a current CUDA context")
    return int(context.value)


try:
    source = cp.full((48 * 3 // 2, 64), 7, dtype=cp.uint8)
    stream = cp.cuda.get_current_stream()
    event = cp.cuda.Event(disable_timing=True)
    event.record(stream)
except Exception as exc:
    skip(f"CUDA allocation/event unavailable ({exc})")

event_pointer = int(getattr(event, "ptr", 0))
if event_pointer == 0:
    skip("CuPy Event does not expose its CUDA handle")
source_pointer = int(source.data.ptr)
frame = mkvcodec.GpuFrame.import_dlpack_nv12(
    source,
    context=current_context(),
    frame_size=(64, 48),
    event=event_pointer,
)
del source
gc.collect()

# CuPy supplies its current stream to GpuPlane.__dlpack__. Native code inserts
# the producer event into that stream before returning the managed tensor.
plane = cp.from_dlpack(frame.plane(0))
assert plane.shape == (48, 64)
assert int(plane.data.ptr) == source_pointer
frame.close()
assert int(cp.sum(plane).get()) == 48 * 64 * 7
del plane
gc.collect()
stream.synchronize()
print("CuPy contiguous NV12 DLPack import/export passed")
