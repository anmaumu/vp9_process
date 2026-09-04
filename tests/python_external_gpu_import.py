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
va_owner_ref = weakref.ref(owner)
va_frame = mkvcodec.GpuFrame.import_va_surface(
    display=0x1000, surface_id=0, device_id=0, frame_size=(64, 48),
    owner=owner, producer_synchronized=True)
del owner
gc.collect()
assert va_owner_ref() is not None
assert va_frame.native_handle["handles"][:2] == (0x1000, 0)
assert va_frame.interop.backend == "intel"
assert va_frame.interop.memory_type == "va_surface"
assert va_frame.supports_interop("VA_API")
assert not va_frame.supports_interop("dlpack")
va_frame.close()
gc.collect()
assert va_owner_ref() is None
for invalid_surface in (-1, 0xFFFFFFFF, 0x100000000):
    try:
        mkvcodec.GpuFrame.import_va_surface(
            display=0x1000, surface_id=invalid_surface, device_id=0,
            frame_size=(64, 48), owner=Owner(), producer_synchronized=True)
    except ValueError:
        pass
    else:
        raise AssertionError("invalid VA surface ID was accepted")

# A native import failure must cancel the holder without retaining its owner.
from unittest.mock import patch
owner = Owner()
failed_va_owner = weakref.ref(owner)
with patch.object(api.native.lib, "mkvc_gpu_frame_import_va_surface", return_value=3):
    try:
        mkvcodec.GpuFrame.import_va_surface(
            display=0x1000, surface_id=0, device_id=0,
            frame_size=(64, 48), owner=owner)
    except ValueError:
        pass
    else:
        raise AssertionError("native VA error was swallowed")
del owner
gc.collect()
assert failed_va_owner() is None

for invalid_target in (0, -1, 0xFFFFFFFFFFFFFFFF):
    try:
        mkvcodec.GpuFrame.import_d3d11_texture(
            texture=0x1000, fence=0x2000, fence_value=invalid_target,
            device_id=0, frame_size=(64, 48), owner=Owner())
    except ValueError:
        pass
    else:
        raise AssertionError("invalid D3D11 fence value accepted")
owner = Owner()
failed_d3d_owner = weakref.ref(owner)
with patch.object(api.native.lib, "mkvc_gpu_frame_import_d3d11_fence", return_value=3):
    try:
        mkvcodec.GpuFrame.import_d3d11_texture(
            texture=0x1000, fence=0x2000, fence_value=1,
            device_id=0, frame_size=(64, 48), owner=owner)
    except ValueError:
        pass
    else:
        raise AssertionError("native D3D11 failure swallowed")
del owner
gc.collect()
assert failed_d3d_owner() is None


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
assert frame.interop.backend == "nvidia"
assert frame.interop.dlpack_export
assert frame.supports_interop("cuda") and frame.supports_interop("dlpack")
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
assert array_frame.supports_interop("cuda")
assert not array_frame.supports_interop("dlpack")
array_frame.close()
gc.collect()
assert array_owner_ref() is None

# Backend auto-selection is deterministic and never silently falls back to CPU
# when the caller requires a GPU-resident path.
Capability = api.BackendCapability
rows = (
    Capability("cpu", "vp9", True, True, False),
    Capability("intel", "vp9", True, True, True),
    Capability("nvidia", "vp9", True, False, True),
    Capability("cpu", "av1", True, True, False),
    Capability("nvidia", "av1", True, True, True),
)
with patch.object(api, "backend_capabilities", return_value=rows):
    assert api._select_backend("vp9", "decode", False) == "nvidia"
    assert api._select_backend("vp9", "encode", False) == "intel"
    assert api._select_backend("av1", "encode", True) == "nvidia"
    assert mkvcodec.select_backend(
        "vp9", decode=True, encode=True, require_gpu_resident=True) == "intel"
with patch.object(api, "backend_capabilities", return_value=(rows[0],)):
    try:
        api._select_backend("vp9", "encode", True)
    except RuntimeError as error:
        assert "GPU-resident" in str(error)
    else:
        raise AssertionError("strict GPU auto-selection silently chose CPU")
with patch.object(api, "backend_capabilities", return_value=(rows[0], rows[2])):
    try:
        mkvcodec.select_backend("vp9", require_gpu_resident=True)
    except RuntimeError as error:
        assert "decode/encode" in str(error)
    else:
        raise AssertionError("pipeline selection accepted decode-only NVIDIA")

runtime_capabilities = mkvcodec.backend_capabilities()
assert runtime_capabilities
assert all(row.backend in ("cpu", "intel", "nvidia") for row in runtime_capabilities)
