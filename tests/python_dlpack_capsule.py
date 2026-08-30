import ctypes as ct
import gc

import _dlpack


class Managed(ct.Structure):
    pass


Deleter = ct.CFUNCTYPE(None, ct.POINTER(Managed))
Managed._fields_ = [
    ("opaque_tensor", ct.c_ubyte * 48),
    ("manager_ctx", ct.c_void_p),
    ("deleter", Deleter),
]


deleted = 0


@Deleter
def delete_tensor(_tensor):
    global deleted
    deleted += 1


def capsule_name(capsule):
    function = ct.pythonapi.PyCapsule_GetName
    function.argtypes = [ct.py_object]
    function.restype = ct.c_char_p
    return function(capsule)


unconsumed = Managed(deleter=delete_tensor)
capsule = _dlpack.capsule_from_address(ct.addressof(unconsumed))
assert capsule_name(capsule) == b"dltensor"
del capsule
gc.collect()
assert deleted == 1

consumed = Managed(deleter=delete_tensor)
capsule = _dlpack.capsule_from_address(ct.addressof(consumed))
set_name = ct.pythonapi.PyCapsule_SetName
set_name.argtypes = [ct.py_object, ct.c_char_p]
set_name.restype = ct.c_int
assert set_name(capsule, b"used_dltensor") == 0
del capsule
gc.collect()
assert deleted == 1
delete_tensor(ct.pointer(consumed))
assert deleted == 2
