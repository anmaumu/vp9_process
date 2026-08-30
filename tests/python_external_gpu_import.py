import gc
import os
import sys
import weakref

native_library, extension_dir, package_dir = sys.argv[1:4]
os.environ["MKVC_LIBRARY_PATH"] = native_library
sys.path.insert(0, package_dir)
sys.path.insert(0, extension_dir)

import _dlpack
import mkvcodec
import mkvcodec._api as api

# Source-tree tests keep the extension in the build directory. Wheels install
# it as mkvcodec._dlpack, so connect the equivalent module explicitly here.
api._dlpack = _dlpack


class Owner:
    pass


owner = Owner()
owner_ref = weakref.ref(owner)
frame = mkvcodec.GpuFrame.import_cuda_pointer(
    pointer=0x1000,
    context=0x2000,
    device_id=0,
    frame_size=(64, 48),
    pitch=64,
    owner=owner,
    producer_synchronized=True,
)
del owner
gc.collect()
assert owner_ref() is not None
assert frame.descriptor["width"] == 64
assert frame.native_handle["handles"][0] == 0x1000
frame.wait(100)
frame.close()
gc.collect()
assert owner_ref() is None

try:
    mkvcodec.GpuFrame.import_cuda_pointer(
        pointer=0x1000,
        context=0x2000,
        device_id=0,
        frame_size=(64, 48),
        pitch=64,
        owner=Owner(),
    )
except ValueError:
    pass
else:
    raise AssertionError("unsynchronized external CUDA import was accepted")
