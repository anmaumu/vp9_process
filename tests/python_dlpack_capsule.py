import ctypes as ct
import gc
import weakref

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

retained_source = Managed(deleter=delete_tensor)
source_capsule = _dlpack.capsule_from_address(ct.addressof(retained_source))


class RetainedOwner:
    pass


retained_owner = RetainedOwner()
retained_owner_ref = weakref.ref(retained_owner)
retained_capsule = _dlpack.retain_owner(source_capsule, retained_owner)
assert retained_capsule is source_capsule
assert capsule_name(source_capsule) == b"dltensor"
assert capsule_name(retained_capsule) == b"dltensor"
del source_capsule, retained_owner
gc.collect()
assert retained_owner_ref() is not None
assert deleted == 2
del retained_capsule
gc.collect()
assert retained_owner_ref() is None
assert deleted == 3


class Owner:
    pass


owner = Owner()
owner_ref = weakref.ref(owner)
user_data, release_address = _dlpack.external_owner_create(owner)
del owner
gc.collect()
assert owner_ref() is not None
_dlpack.external_owner_cancel(user_data)
gc.collect()
assert owner_ref() is None

owner = Owner()
owner_ref = weakref.ref(owner)
user_data, release_address = _dlpack.external_owner_create(owner)
del owner
gc.collect()
assert owner_ref() is not None
ReleaseOwner = ct.CFUNCTYPE(None, ct.c_void_p)
ReleaseOwner(release_address)(user_data)
gc.collect()
assert owner_ref() is None
