"""Test-only external OpenCL producer; no OpenCL code enters the shipped library.

ABI source: Khronos cl_intel_va_api_media_sharing, OpenCL 1.2.
The shared objects are images, NOT USM pointers or DLPack tensors.
"""
import ctypes as ct
from contextlib import ExitStack

P, U, I, Z = ct.c_void_p, ct.c_uint, ct.c_int, ct.c_size_t


class Unsupported(RuntimeError):
    pass


def check(code):
    if code != 0:
        raise RuntimeError(f"External OpenCL/VA operation failed: {code}")


def bind(library, name, result, *args):
    function = getattr(library, name)
    function.restype, function.argtypes = result, list(args)
    return function


class VaOwner:
    live = 0
    peak = 0
    released = 0

    def __init__(self, source, width, height):
        self.source = source  # Retains the display, even after Capture.close().
        self.display = source.native_handle["handles"][0]
        self.surface = U(0xFFFFFFFF)
        self.va = ct.CDLL("libva.so.2")
        self.destroy = bind(self.va, "vaDestroySurfaces", I, P, ct.POINTER(U), I)
        create = bind(self.va, "vaCreateSurfaces", I, P, U, U, U, ct.POINTER(U), U, P, U)
        check(create(self.display, 1, width, height, ct.byref(self.surface), 1, None, 0))
        VaOwner.live += 1
        VaOwner.peak = max(VaOwner.peak, VaOwner.live)

    def close(self):
        if self.surface.value != 0xFFFFFFFF:
            check(self.destroy(self.display, ct.byref(self.surface), 1))
            self.surface.value = 0xFFFFFFFF
            VaOwner.live -= 1
            VaOwner.released += 1
            self.source = None

    def __del__(self):
        if getattr(self, "surface", U(0xFFFFFFFF)).value != 0xFFFFFFFF:
            self.close()


def invert_luma(source, output, width, height):
    """Write an inverted Y plane and neutral UV into another shared VA surface.

    Does not call read/map/write-buffer APIs; explicit clFinish covers the
    release of shared images before the caller declares producer_synchronized.
    API sharing does not by itself prove driver-internal zero-copy behavior.
    """
    try:
        cl = ct.CDLL("libOpenCL.so.1")
    except OSError as error:
        raise Unsupported(str(error)) from error
    platforms_fn = bind(cl, "clGetPlatformIDs", I, U, ct.POINTER(P), ct.POINTER(U))
    address = bind(cl, "clGetExtensionFunctionAddressForPlatform", P, P, ct.c_char_p)
    count = U()
    if platforms_fn(0, None, ct.byref(count)) != 0 or count.value == 0:
        raise Unsupported("No OpenCL platform")
    platforms = (P * count.value)()
    check(platforms_fn(count, platforms, None))
    device, selected = P(), None
    for platform in platforms:
        pointer = address(platform, b"clGetDeviceIDsFromVA_APIMediaAdapterINTEL")
        if not pointer:
            continue
        get_devices = ct.CFUNCTYPE(I, P, U, P, U, U, ct.POINTER(P), ct.POINTER(U))(pointer)
        if get_devices(platform, 0x4094, output.display, 0x4095, 1,
                       ct.byref(device), None) == 0:
            selected = platform
            break
    if selected is None:
        raise Unsupported("No preferred OpenCL device shares the decoder VA display")

    def extension(name, result, *args):
        pointer = address(selected, name.encode())
        if not pointer:
            raise Unsupported(f"Missing {name}")
        return ct.CFUNCTYPE(result, *args)(pointer)

    create_image = extension("clCreateFromVA_APIMediaSurfaceINTEL", P, P,
                             ct.c_ulonglong, ct.POINTER(U), U, ct.POINTER(I))
    acquire = extension("clEnqueueAcquireVA_APIMediaSurfacesINTEL", I,
                        P, U, ct.POINTER(P), U, P, P)
    release = extension("clEnqueueReleaseVA_APIMediaSurfacesINTEL", I,
                        P, U, ct.POINTER(P), U, P, P)
    context_fn = bind(cl, "clCreateContext", P, ct.POINTER(ct.c_ssize_t), U,
                      ct.POINTER(P), P, P, ct.POINTER(I))
    queue_fn = bind(cl, "clCreateCommandQueue", P, P, P, ct.c_ulonglong, ct.POINTER(I))
    program_fn = bind(cl, "clCreateProgramWithSource", P, P, U,
                      ct.POINTER(ct.c_char_p), ct.POINTER(Z), ct.POINTER(I))
    build = bind(cl, "clBuildProgram", I, P, U, ct.POINTER(P), ct.c_char_p, P, P)
    build_log = bind(cl, "clGetProgramBuildInfo", I, P, P, U, Z, P, ct.POINTER(Z))
    kernel_fn = bind(cl, "clCreateKernel", P, P, ct.c_char_p, ct.POINTER(I))
    set_arg = bind(cl, "clSetKernelArg", I, P, U, Z, P)
    enqueue = bind(cl, "clEnqueueNDRangeKernel", I, P, P, U, P, ct.POINTER(Z), P, U, P, P)
    finish = bind(cl, "clFinish", I, P)
    code = b"""
    const sampler_t smp = CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_NONE | CLK_FILTER_NEAREST;
    __kernel void invert(read_only image2d_t src, write_only image2d_t dst) {
        int2 p = (int2)(get_global_id(0), get_global_id(1));
        float y = read_imagef(src, smp, p).x;
        write_imagef(dst, p, (float4)(1.0f-y, 0, 0, 1));
    }
    __kernel void neutral(write_only image2d_t uv) {
        int2 p = (int2)(get_global_id(0), get_global_id(1));
        write_imagef(uv, p, (float4)(128.0f/255.0f, 128.0f/255.0f, 0, 1));
    }
    """
    with ExitStack() as stack:
        error = I()

        def own(handle, release_name):
            check(error.value)
            if not handle:
                raise RuntimeError("OpenCL returned a null object")
            stack.callback(bind(cl, release_name, I, P), handle)
            return handle

        props = (ct.c_ssize_t * 5)(0x1084, selected, 0x4097, output.display, 0)
        context = own(context_fn(props, 1, ct.byref(device), None, None, ct.byref(error)),
                      "clReleaseContext")
        queue = own(queue_fn(context, device, 0, ct.byref(error)), "clReleaseCommandQueue")
        source_id = U(source.native_handle["handles"][1])
        src = own(create_image(context, 4, ct.byref(source_id), 0, ct.byref(error)), "clReleaseMemObject")
        dst = own(create_image(context, 2, ct.byref(output.surface), 0, ct.byref(error)), "clReleaseMemObject")
        uv = own(create_image(context, 2, ct.byref(output.surface), 1, ct.byref(error)), "clReleaseMemObject")
        program = own(program_fn(context, 1, (ct.c_char_p * 1)(code), None, ct.byref(error)),
                      "clReleaseProgram")
        if build(program, 1, ct.byref(device), b"-cl-std=CL1.2", None, None) != 0:
            log = ct.create_string_buffer(8192)
            build_log(program, device, 0x1183, len(log), log, None)
            raise RuntimeError(log.value.decode(errors="replace"))
        invert = own(kernel_fn(program, b"invert", ct.byref(error)), "clReleaseKernel")
        neutral = own(kernel_fn(program, b"neutral", ct.byref(error)), "clReleaseKernel")
        for kernel, values in ((invert, (src, dst)), (neutral, (uv,))):
            for index, value in enumerate(values):
                argument = P(value)
                check(set_arg(kernel, index, ct.sizeof(argument), ct.byref(argument)))
        objects = (P * 3)(src, dst, uv)
        check(acquire(queue, 3, objects, 0, None, None))
        try:
            check(enqueue(queue, invert, 2, None, (Z * 2)(width, height), None, 0, None, None))
            check(enqueue(queue, neutral, 2, None, (Z * 2)(width // 2, height // 2), None, 0, None, None))
        finally:
            released = release(queue, 3, objects, 0, None, None)
            finished = finish(queue)
            check(released)
            check(finished)
