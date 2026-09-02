"""Synthetic host transfers through RTLD_LOCAL dlsym and an extension pointer."""
import ctypes as ct
import sys

library = ct.CDLL(sys.argv[1])
p, u, z = ct.c_void_p, ct.c_uint, ct.c_size_t
get = library.vaGetImage
get.argtypes = [p, u, ct.c_int, ct.c_int, u, u, u]
get.restype = ct.c_int
assert get(None, 1, 2, 3, 4, 5, 6) == 7
read = library.clEnqueueReadBuffer
read.argtypes = [p, p, u, z, z, p, u, p, p]
read.restype = ct.c_int
assert read(None, None, 1, 0, 8, None, 0, None, None) == 7
lookup = library.clGetExtensionFunctionAddressForPlatform
lookup.argtypes, lookup.restype = [p, ct.c_char_p], p
pointer = lookup(None, b"clEnqueueReadImage")
image_read = ct.CFUNCTYPE(ct.c_int, p, p, u, p, p, z, z, p, u, p, p)(pointer)
assert image_read(None, None, 1, None, None, 64, 0, None, 0, None, None) == 7
assert lookup(None, b"unknown") is None

# Exercise every remaining wrapper, including pointer returns and stack arguments.
for name, result, types, values in (
    ("vaPutImage", ct.c_int, [p, u, u, ct.c_int, ct.c_int, u, u, ct.c_int, ct.c_int, u, u],
     [None, 1, 2, -3, -4, 5, 6, -7, -8, 9, 10]),
    ("vaDeriveImage", ct.c_int, [p, u, p], [None, 1, None]),
    ("vaMapBuffer", ct.c_int, [p, u, p], [None, 1, None]),
    ("vaMapBuffer2", ct.c_int, [p, u, p, u], [None, 1, None, 2]),
    ("clEnqueueWriteBuffer", ct.c_int, [p, p, u, z, z, p, u, p, p],
     [None, None, 1, 0, 8, None, 0, None, None]),
    ("clEnqueueWriteImage", ct.c_int, [p, p, u, p, p, z, z, p, u, p, p],
     [None, None, 1, None, None, 64, 0, None, 0, None, None]),
    ("clEnqueueMapBuffer", p, [p, p, u, ct.c_uint64, z, z, u, p, p, p],
     [None, None, 1, 1 << 40, 0, 8, 0, None, None, None]),
    ("clEnqueueMapImage", p, [p, p, u, ct.c_uint64, p, p, p, p, u, p, p, p],
     [None, None, 1, 1 << 40, None, None, None, None, 0, None, None, None]),
    ("clEnqueueNDRangeKernel", ct.c_int, [p, p, u, p, p, p, u, p, p],
     [None, None, 2, None, None, None, 0, None, None]),
    ("clEnqueueAcquireVA_APIMediaSurfacesINTEL", ct.c_int, [p, u, p, u, p, p],
     [None, 2, None, 0, None, None]),
    ("clEnqueueReleaseVA_APIMediaSurfacesINTEL", ct.c_int, [p, u, p, u, p, p],
     [None, 2, None, 0, None, None]),
):
    call = ct.CFUNCTYPE(result, *types)(lookup(None, name.encode()))
    assert call(*values) == 7, name
