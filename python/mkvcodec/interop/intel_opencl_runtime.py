"""OpenCL VA-sharing runtime for fused Intel NV12 conversion."""

from __future__ import annotations

import ctypes as ct
import threading
import time


P = ct.c_void_p
U = ct.c_uint
I = ct.c_int  # noqa: E741 - mirrors OpenCL's cl_int abbreviation
Z = ct.c_size_t


def _bind(library: object, name: str, result: object, *arguments: object):
    function = getattr(library, name)
    function.restype = result
    function.argtypes = list(arguments)
    return function


def _check(result: int, operation: str) -> None:
    if result != 0:
        raise RuntimeError(f"{operation} failed: {result}")


class OpenClCompletion:
    """Own a release event and per-submission shared OpenCL images."""

    def __init__(
        self,
        runtime: "IntelOpenClRuntime",
        event: P,
        images: tuple[P, ...],
        source_owner: object,
    ) -> None:
        self._runtime = runtime
        self._event = event
        self._images = images
        self._source_owner = source_owner
        self._closed = False

    def wait(self, timeout_ms: int) -> None:
        """Wait for the VA release event, honoring a finite poll timeout."""
        if self._closed:
            return
        runtime = self._runtime
        if timeout_ms == 0xFFFFFFFF:
            _check(
                runtime.wait_events(1, ct.byref(self._event)),
                "OpenCL completion wait",
            )
            return
        deadline = time.monotonic() + timeout_ms / 1000.0
        status = I()
        while True:
            _check(
                runtime.event_info(
                    self._event,
                    0x11D3,
                    ct.sizeof(status),
                    ct.byref(status),
                    None,
                ),
                "OpenCL completion query",
            )
            if status.value == 0:
                return
            if status.value < 0:
                raise RuntimeError(f"OpenCL command failed: {status.value}")
            if time.monotonic() >= deadline:
                raise TimeoutError("OpenCL RGB conversion timed out")
            time.sleep(0.0005)

    def close(self) -> None:
        """Release the event and shared images after completion."""
        if self._closed:
            return
        self.wait(0xFFFFFFFF)
        self._closed = True
        failure: BaseException | None = None
        try:
            _check(self._runtime.release_event(self._event), "OpenCL event release")
        except BaseException as exception:
            failure = exception
        for image in reversed(self._images):
            try:
                _check(
                    self._runtime.release_mem(image),
                    "OpenCL shared image release",
                )
            except BaseException as exception:
                if failure is None:
                    failure = exception
        self._images = ()
        self._event = P()
        source_owner, self._source_owner = self._source_owner, None
        try:
            source_owner.close()
        except BaseException as exception:
            if failure is None:
                failure = exception
        if failure is not None:
            raise failure

    def __del__(self) -> None:
        try:
            self.close()
        except Exception:
            pass


class IntelOpenClRuntime:
    """Persistent OpenCL context, queue, and fused VA-NV12 conversion kernel."""

    _CODE = b"""
    const sampler_t smp = CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_NONE |
                          CLK_FILTER_NEAREST;
    __kernel void nv12_rgba(
        read_only image2d_t y_plane,
        read_only image2d_t uv_plane,
        write_only image2d_t dst,
        float y_offset,
        float y_multiplier,
        float red_v,
        float green_u,
        float green_v,
        float blue_u,
        int bgr,
        int channels
    ) {
        int2 p = (int2)(get_global_id(0), get_global_id(1));
        float y = (read_imagef(y_plane, smp, p).x * 255.0f - y_offset)
            * y_multiplier;
        float2 uv = read_imagef(uv_plane, smp, p / 2).xy * 255.0f - 128.0f;
        float r = clamp(y + red_v * uv.y, 0.0f, 255.0f);
        float g = clamp(y + green_u * uv.x + green_v * uv.y, 0.0f, 255.0f);
        float b = clamp(y + blue_u * uv.x, 0.0f, 255.0f);
        float first = bgr ? b : r;
        float third = bgr ? r : b;
        int x = p.x * channels;
        write_imagef(dst, (int2)(x, p.y), first / 255.0f);
        write_imagef(dst, (int2)(x + 1, p.y), g / 255.0f);
        write_imagef(dst, (int2)(x + 2, p.y), third / 255.0f);
        if (channels == 4)
            write_imagef(dst, (int2)(x + 3, p.y), 1.0f);
    }
    """

    def __init__(self, display: int) -> None:
        self.display = display
        self._lock = threading.Lock()
        self._closed = False
        self.cl = ct.CDLL("libOpenCL.so.1")
        platforms = _bind(self.cl, "clGetPlatformIDs", I, U, ct.POINTER(P), ct.POINTER(U))
        address = _bind(
            self.cl,
            "clGetExtensionFunctionAddressForPlatform",
            P,
            P,
            ct.c_char_p,
        )
        count = U()
        _check(platforms(0, None, ct.byref(count)), "OpenCL platform discovery")
        values = (P * count.value)()
        _check(platforms(count, values, None), "OpenCL platform enumeration")
        self.platform = None
        self.device = P()
        for platform in values:
            pointer = address(platform, b"clGetDeviceIDsFromVA_APIMediaAdapterINTEL")
            if not pointer:
                continue
            get_devices = ct.CFUNCTYPE(I, P, U, P, U, U, ct.POINTER(P), ct.POINTER(U))(pointer)
            if (
                get_devices(
                    platform,
                    0x4094,
                    display,
                    0x4095,
                    1,
                    ct.byref(self.device),
                    None,
                )
                == 0
            ):
                self.platform = platform
                break
        if self.platform is None:
            raise RuntimeError("no OpenCL device shares the decoder VA display")

        def extension(name: str, result: object, *arguments: object):
            pointer = address(self.platform, name.encode())
            if not pointer:
                raise RuntimeError(f"missing OpenCL extension function {name}")
            return ct.CFUNCTYPE(result, *arguments)(pointer)

        self.create_image = extension(
            "clCreateFromVA_APIMediaSurfaceINTEL",
            P,
            P,
            ct.c_ulonglong,
            ct.POINTER(U),
            U,
            ct.POINTER(I),
        )
        self.acquire = extension(
            "clEnqueueAcquireVA_APIMediaSurfacesINTEL",
            I,
            P,
            U,
            ct.POINTER(P),
            U,
            P,
            P,
        )
        self.release = extension(
            "clEnqueueReleaseVA_APIMediaSurfacesINTEL",
            I,
            P,
            U,
            ct.POINTER(P),
            U,
            P,
            ct.POINTER(P),
        )
        context_create = _bind(
            self.cl,
            "clCreateContext",
            P,
            ct.POINTER(ct.c_ssize_t),
            U,
            ct.POINTER(P),
            P,
            P,
            ct.POINTER(I),
        )
        queue_create = _bind(
            self.cl,
            "clCreateCommandQueue",
            P,
            P,
            P,
            ct.c_ulonglong,
            ct.POINTER(I),
        )
        program_create = _bind(
            self.cl,
            "clCreateProgramWithSource",
            P,
            P,
            U,
            ct.POINTER(ct.c_char_p),
            ct.POINTER(Z),
            ct.POINTER(I),
        )
        build = _bind(
            self.cl,
            "clBuildProgram",
            I,
            P,
            U,
            ct.POINTER(P),
            ct.c_char_p,
            P,
            P,
        )
        build_info = _bind(self.cl, "clGetProgramBuildInfo", I, P, P, U, Z, P, ct.POINTER(Z))
        kernel_create = _bind(self.cl, "clCreateKernel", P, P, ct.c_char_p, ct.POINTER(I))
        self.set_argument = _bind(self.cl, "clSetKernelArg", I, P, U, Z, P)
        self.enqueue = _bind(
            self.cl,
            "clEnqueueNDRangeKernel",
            I,
            P,
            P,
            U,
            P,
            ct.POINTER(Z),
            P,
            U,
            P,
            P,
        )
        self.flush = _bind(self.cl, "clFlush", I, P)
        self.finish = _bind(self.cl, "clFinish", I, P)
        self.wait_events = _bind(self.cl, "clWaitForEvents", I, U, ct.POINTER(P))
        self.event_info = _bind(self.cl, "clGetEventInfo", I, P, U, Z, P, ct.POINTER(Z))
        self.release_event = _bind(self.cl, "clReleaseEvent", I, P)
        self.release_mem = _bind(self.cl, "clReleaseMemObject", I, P)
        self.release_kernel = _bind(self.cl, "clReleaseKernel", I, P)
        self.release_program = _bind(self.cl, "clReleaseProgram", I, P)
        self.release_queue = _bind(self.cl, "clReleaseCommandQueue", I, P)
        self.release_context = _bind(self.cl, "clReleaseContext", I, P)

        error = I()
        properties = (ct.c_ssize_t * 5)(0x1084, self.platform, 0x4097, display, 0)
        self.context = context_create(
            properties, 1, ct.byref(self.device), None, None, ct.byref(error)
        )
        _check(error.value, "OpenCL context creation")
        self.queue = queue_create(self.context, self.device, 0, ct.byref(error))
        _check(error.value, "OpenCL queue creation")
        self.program = program_create(
            self.context,
            1,
            (ct.c_char_p * 1)(self._CODE),
            None,
            ct.byref(error),
        )
        _check(error.value, "OpenCL program creation")
        result = build(
            self.program,
            1,
            ct.byref(self.device),
            b"-cl-std=CL1.2",
            None,
            None,
        )
        if result != 0:
            log = ct.create_string_buffer(8192)
            build_info(self.program, self.device, 0x1183, len(log), log, None)
            raise RuntimeError(log.value.decode(errors="replace"))
        self.kernel = kernel_create(self.program, b"nv12_rgba", ct.byref(error))
        _check(error.value, "OpenCL kernel creation")

    def _image(self, surface: int, plane: int, flags: int) -> P:
        error = I()
        surface_id = U(surface)
        image = self.create_image(self.context, flags, ct.byref(surface_id), plane, ct.byref(error))
        _check(error.value, "OpenCL VA image sharing")
        if not image:
            raise RuntimeError("OpenCL returned a null shared image")
        return P(image)

    def submit(
        self,
        source: object,
        carrier: object,
        width: int,
        height: int,
        coefficients: tuple[float, ...],
        *,
        bgr: bool,
        channels: int,
    ) -> OpenClCompletion:
        """Enqueue one fused conversion and return without calling clFinish."""
        with self._lock:
            if self._closed:
                raise RuntimeError("Intel OpenCL runtime is closed")
            source_surface = int(source.native_handle["handles"][1])
            images: tuple[P, ...] = ()
            acquired = False
            source_owner = source.retain()
            try:
                y = self._image(source_surface, 0, 4)
                images = (y,)
                uv = self._image(source_surface, 1, 4)
                images = (y, uv)
                output = self._image(int(carrier.surface.value), 0, 2)
                images = (y, uv, output)
                for index, value in enumerate(images):
                    argument = P(value.value)
                    _check(
                        self.set_argument(
                            self.kernel,
                            index,
                            ct.sizeof(argument),
                            ct.byref(argument),
                        ),
                        "OpenCL image argument",
                    )
                if len(coefficients) != 6:
                    raise ValueError("RGB conversion requires six coefficients")
                for index, value in enumerate(coefficients, start=3):
                    argument = ct.c_float(value)
                    _check(
                        self.set_argument(
                            self.kernel,
                            index,
                            ct.sizeof(argument),
                            ct.byref(argument),
                        ),
                        "OpenCL coefficient argument",
                    )
                order = I(1 if bgr else 0)
                _check(
                    self.set_argument(self.kernel, 9, ct.sizeof(order), ct.byref(order)),
                    "OpenCL channel-order argument",
                )
                channel_count = I(channels)
                _check(
                    self.set_argument(
                        self.kernel,
                        10,
                        ct.sizeof(channel_count),
                        ct.byref(channel_count),
                    ),
                    "OpenCL channel-count argument",
                )
                objects = (P * len(images))(*(image.value for image in images))
                _check(
                    self.acquire(self.queue, len(images), objects, 0, None, None),
                    "OpenCL VA image acquire",
                )
                acquired = True
                _check(
                    self.enqueue(
                        self.queue,
                        self.kernel,
                        2,
                        None,
                        (Z * 2)(width, height),
                        None,
                        0,
                        None,
                        None,
                    ),
                    "OpenCL fused RGB enqueue",
                )
                event = P()
                _check(
                    self.release(
                        self.queue,
                        len(images),
                        objects,
                        0,
                        None,
                        ct.byref(event),
                    ),
                    "OpenCL VA image release enqueue",
                )
                acquired = False
                _check(self.flush(self.queue), "OpenCL queue flush")
                completion = OpenClCompletion(self, event, images, source_owner)
                source_owner = None
                return completion
            except BaseException:
                if acquired:
                    objects = (P * len(images))(*(image.value for image in images))
                    self.release(self.queue, len(images), objects, 0, None, None)
                    self.finish(self.queue)
                for image in reversed(images):
                    self.release_mem(image)
                if source_owner is not None:
                    source_owner.close()
                raise

    def close(self) -> None:
        """Drain and destroy persistent OpenCL objects."""
        with self._lock:
            if self._closed:
                return
            self._closed = True
            self.finish(self.queue)
            for function, value in (
                (self.release_kernel, self.kernel),
                (self.release_program, self.program),
                (self.release_queue, self.queue),
                (self.release_context, self.context),
            ):
                _check(function(value), "OpenCL runtime release")

    def __del__(self) -> None:
        try:
            self.close()
        except Exception:
            pass


__all__ = ["IntelOpenClRuntime", "OpenClCompletion"]
