import gc
import ctypes as ct
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


class DLDevice(ct.Structure):
    _fields_ = [("device_type", ct.c_int), ("device_id", ct.c_int)]


class DLDataType(ct.Structure):
    _fields_ = [("code", ct.c_uint8), ("bits", ct.c_uint8),
                ("lanes", ct.c_uint16)]


class DLTensor(ct.Structure):
    _fields_ = [
        ("data", ct.c_void_p), ("device", DLDevice), ("ndim", ct.c_int),
        ("dtype", DLDataType), ("shape", ct.POINTER(ct.c_int64)),
        ("strides", ct.POINTER(ct.c_int64)), ("byte_offset", ct.c_uint64),
    ]


class DLManagedTensor(ct.Structure):
    pass


DLDeleter = ct.CFUNCTYPE(None, ct.POINTER(DLManagedTensor))
DLManagedTensor._fields_ = [
    ("dl_tensor", DLTensor), ("manager_ctx", ct.c_void_p),
    ("deleter", DLDeleter),
]


def make_dlpack(rows=72):
    deleted = []
    shape = (ct.c_int64 * 2)(rows, 64)
    strides = (ct.c_int64 * 2)(64, 1)

    @DLDeleter
    def deleter(_):
        deleted.append(True)

    managed = DLManagedTensor(
        DLTensor(ct.c_void_p(0x3000), DLDevice(2, 0), 2,
                 DLDataType(1, 8, 1), shape, strides, 0),
        None, deleter,
    )

    class Provider:
        def __dlpack__(self):
            return _dlpack.capsule_from_address(ct.addressof(managed))

    # Keep ctypes storage/callback alive independently of the consumed provider.
    return Provider(), deleted, (shape, strides, managed, deleter)


provider, deleted, storage = make_dlpack()
dlpack_frame = mkvcodec.GpuFrame.import_dlpack_nv12(
    provider, context=0x2000, frame_size=(64, 48),
    producer_synchronized=True,
)
assert dlpack_frame.native_handle["handles"][0] == 0x3000
assert dlpack_frame.descriptor["pitches"][0] == 64
assert deleted == []
dlpack_frame.close()
gc.collect()
assert deleted == [True]

invalid_provider, invalid_deleted, invalid_storage = make_dlpack(rows=71)
try:
    mkvcodec.GpuFrame.import_dlpack_nv12(
        invalid_provider, context=0x2000, frame_size=(64, 48),
        producer_synchronized=True,
    )
except ValueError:
    pass
else:
    raise AssertionError("invalid DLPack NV12 layout was accepted")
gc.collect()
assert invalid_deleted == [True]

array_owner = Owner()
array_owner_ref = weakref.ref(array_owner)
array_frame = mkvcodec.GpuFrame.import_cuda_array(
    array=0x4000,
    context=0x2000,
    device_id=0,
    frame_size=(64, 48),
    owner=array_owner,
    producer_synchronized=True,
)
del array_owner
gc.collect()
assert array_owner_ref() is not None
assert array_frame.descriptor["memory_type"] == 4
assert array_frame.native_handle["type"] == 4
array_frame.close()
gc.collect()
assert array_owner_ref() is None
