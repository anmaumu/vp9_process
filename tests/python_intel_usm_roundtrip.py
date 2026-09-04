"""Experimental device-USM/DLPack -> linear DMA-BUF VA -> AV1 encode proof.

The decoder's tiled image is explicitly materialized into a different linear
GPU allocation by the external OpenCL kernel. This edge is NOT zero-copy.
No adapter in this file is a shipped/public mkvcodec API.
"""
import ctypes as ct
from contextlib import nullcontext
import gc
import json
import os
from pathlib import Path
from media_oracle import run_oracle
import sys
import tempfile
import weakref

native, extension, package, fixture, helper, output = sys.argv[1:7]
os.environ["MKVC_LIBRARY_PATH"] = native
# Use the isolated environment's NumPy, not the fixture's vendored fallback.
import numpy as np
import dpctl
import dpctl.memory
import dpnp
import dpnp.tensor
sys.path[:0] = [package, extension]
import mkvcodec
import mkvcodec._api as api
import _dlpack
api._dlpack = _dlpack
from intel_va_opencl_support import bind, check, invert_luma, P, U, I, OpenClReuseSession, reuse_program_enabled
from intel_va_prime_support import Prime, Attribute, export_layout


class ExportableAllocation:
    """Dedicated L0 device allocation explicitly requesting DMA-BUF export."""
    released = 0
    def __init__(self, queue, library, size):
        self.queue, self.size = queue, size
        self.pointer = P()
        self.free = bind(library, "mkvc_test_sycl_free", I, P, P)
        allocate = bind(library, "mkvc_test_sycl_alloc_exportable", I, P, ct.c_uint64, ct.POINTER(P))
        check(allocate(queue.addressof_ref(), size, ct.byref(self.pointer)))

    @property
    def __sycl_usm_array_interface__(self):
        return {"data": (self.pointer.value, False), "shape": (self.size,), "strides": None,
                "typestr": "|u1", "version": 1, "syclobj": self.queue}

    def __del__(self):
        if self.pointer.value:
            check(self.free(self.queue.addressof_ref(), self.pointer))
            ExportableAllocation.released += 1


class UsmVaOwner:
    """Keep display, SYCL allocation and VA import alive together."""
    released = 0

    def __init__(self, source, array, library, width, height):
        self.source, self.array = source, array
        self.surface = U(0xFFFFFFFF)
        self.display = source.native_handle["handles"][0]
        va = ct.CDLL("libva.so.2")
        self.destroy = bind(va, "vaDestroySurfaces", I, P, ct.POINTER(U), I)
        export = bind(library, "mkvc_test_sycl_export_fd", I, P, P, ct.POINTER(I), ct.POINTER(ct.c_uint64), ct.POINTER(ct.c_uint64))
        fd, size, offset = I(-1), ct.c_uint64(), ct.c_uint64()
        queue = array.sycl_queue
        pointer = array.__sycl_usm_array_interface__["data"][0]
        check(export(queue.addressof_ref(), pointer, ct.byref(fd), ct.byref(size), ct.byref(offset)))
        try:
            stat = os.fstat(fd.value)
            self.dma_identity = [stat.st_dev, stat.st_ino]
            if stat.st_size != size.value:
                raise RuntimeError(f"Pooled USM physical offset cannot be established safely: {size.value}/{stat.st_size}")
            if size.value < offset.value + width * height * 3 // 2 or size.value > 0xFFFFFFFF:
                raise RuntimeError("USM allocation size is not VA representable")
            prime = Prime()
            prime.fourcc, prime.width, prime.height = 0x3231564E, width, height
            prime.num_objects, prime.num_layers = 1, 1
            prime.objects[0].fd, prime.objects[0].size = fd.value, size.value
            prime.layers[0].format, prime.layers[0].planes = prime.fourcc, 2
            prime.layers[0].pitches[0] = prime.layers[0].pitches[1] = width
            prime.layers[0].offsets[0] = offset.value
            prime.layers[0].offsets[1] = offset.value + width * height
            attrs = (Attribute * 2)()
            attrs[0].type, attrs[0].flags, attrs[0].value.type = 6, 2, 1
            attrs[0].value.value.i = 0x40000000
            attrs[1].type, attrs[1].flags, attrs[1].value.type = 7, 2, 3
            attrs[1].value.value.p = ct.addressof(prime)
            create = bind(va, "vaCreateSurfaces", I, P, U, U, U, ct.POINTER(U), U, P, U)
            check(create(self.display, 1, width, height, ct.byref(self.surface), 1, attrs, 2))
        finally:
            if fd.value >= 0:
                os.close(fd.value)

    def __del__(self):
        if self.surface.value != 0xFFFFFFFF:
            check(self.destroy(self.display, ct.byref(self.surface), 1))
            self.surface.value = 0xFFFFFFFF
            UsmVaOwner.released += 1


class SyclEventOwner:
    """Retain a native SYCL event and the allocation owner as one frame lease."""
    released = 0

    def __init__(self, resource_owner, array, library):
        self.resource_owner = resource_owner
        self.handle, self.event = P(), P()
        self.free = bind(library, "mkvc_test_sycl_event_free", I, P)
        create = bind(library, "mkvc_test_sycl_barrier_event", I, P,
                      ct.POINTER(P), ct.POINTER(P))
        check(create(array.sycl_queue.addressof_ref(), ct.byref(self.handle),
                     ct.byref(self.event)))

    def __del__(self):
        if self.handle.value:
            check(self.free(self.handle))
            self.handle.value = None
            SyclEventOwner.released += 1


class QueueBoundPlane:
    """Force a real consumer SYCL queue through the public DLPack stream slot."""
    def __init__(self, plane, stream):
        self.plane, self.stream = plane, stream

    def __dlpack_device__(self):
        return self.plane.__dlpack_device__()

    def __dlpack__(self, **kwargs):
        kwargs["stream"] = self.stream
        return self.plane.__dlpack__(**kwargs)


def main():
    Path(output).write_text('{"validation":"not_completed"}\n')
    reuse = reuse_program_enabled()
    library = ct.CDLL(helper)
    queue = dpctl.SyclQueue("level_zero:gpu:0")
    if "B580" not in queue.name:
        raise RuntimeError("This experimental qualification requires the expected Arc B580")
    dependency_registrations = 0
    queue_wait_event = bind(library, "mkvc_test_sycl_queue_wait_event", I, P, P)

    def register_dependency(event, consumer_stream):
        nonlocal dependency_registrations
        check(queue_wait_event(P(consumer_stream), P(event)))
        dependency_registrations += 1
    with mkvcodec.VideoCapture(fixture, backend="cpu", prefetch=0) as cpu:
        reference = cpu.read_i420().y.astype(np.float32)
    with mkvcodec.VideoCapture(fixture, backend="intel", prefetch=0, require_gpu_resident=True) as capture:
        source = capture.read_surface()
        source.wait(5000)
        desc = source.descriptor
    width, height = desc["width"], desc["height"]
    frames = int(os.environ.get("MKVC_USM_TEST_FRAMES", "8"))
    assert 1 <= frames <= 240
    with tempfile.TemporaryDirectory() as directory:
        path = str(Path(directory) / "usm.webm")
        with mkvcodec.VideoWriter(path, backend="intel", codec="av1", frame_size=(width, height),
                                 fps=30, queue_size=0, require_gpu_resident=True) as writer, \
                (OpenClReuseSession() if reuse else nullcontext()) as session:
            for index in range(frames):
                allocation = ExportableAllocation(queue, library, 2 * 1024**2)
                memory = dpctl.memory.as_usm_memory(allocation)
                view = dpnp.tensor.usm_ndarray((height * 3 // 2, width), dtype="u1", buffer=memory)
                array = dpnp.asarray(view, copy=False)
                owner = UsmVaOwner(source, array, library, width, height)
                layout = export_layout(owner.display, owner.surface.value)
                if not layout["linear_modifier"]:
                    raise RuntimeError("VA import changed linear USM layout")
                assert len(layout["objects"]) == 1
                assert layout["objects"][0]["dma_identity"] == owner.dma_identity
                assert layout["fourcc"] == 0x3231564E and (layout["width"], layout["height"]) == (width, height)
                assert len(layout["layers"]) == 2
                assert [layer["planes"] for layer in layout["layers"]] == [
                    [{"object": 0, "offset": 0, "pitch": width}],
                    [{"object": 0, "offset": width * height, "pitch": width}]]
                identity = invert_luma(source, owner, width, height, frame_index=index, session=session)
                if identity["pci"] != "0000:83:00.0":
                    raise RuntimeError("OpenCL and SYCL are not using the expected same GPU")
                # Promote the synchronized device-USM allocation through the
                # public frame/DLPack lease. This preserves pointer identity;
                # the earlier tiled decode -> linear USM edge remains a GPU copy.
                pointer = array.__sycl_usm_array_interface__["data"][0]
                event_owner = SyclEventOwner(owner, array, library)
                usm_frame = mkvcodec.GpuFrame.import_usm_nv12(
                    pointer=pointer,
                    context=array.sycl_queue.sycl_context.addressof_ref(),
                    queue=array.sycl_queue.addressof_ref(),
                    device_id=0, frame_size=(width, height), pitch=width,
                    owner=event_owner, pts_ns=index * 33333333,
                    event=event_owner.event.value,
                    dependency_registrar=register_dependency)
                shared = dpnp.from_dlpack(QueueBoundPlane(
                    usm_frame.plane(0), array.sycl_queue.addressof_ref()))
                assert shared.__sycl_usm_array_interface__["data"][0] == array.__sycl_usm_array_interface__["data"][0]
                shared[:] = 255 - shared[:]  # External Python GPU processing.
                shared.sycl_queue.wait()  # The DLPack consumer may choose another queue.
                del shared
                gc.collect()
                usm_frame.close()
                del event_owner
                gc.collect()
                imported = mkvcodec.GpuFrame.import_va_surface(
                    display=owner.display, surface_id=owner.surface.value, device_id=desc["device_id"],
                    frame_size=(width, height), pts_ns=index * 33333333,
                    owner=owner, producer_synchronized=True)
                owner_ref = weakref.ref(owner)
                writer.write_surface(imported)
                imported.close()
                del imported, owner, array, usm_frame, allocation, view, memory
                gc.collect()
                # Runtime may have already retired non-anchor owners.
            assert writer.metrics.copy_path == "zero_copy"  # Final import boundary only.
            if session is not None:
                assert session.builds == 1 and session.completed_calls == frames
        gc.collect()
        assert UsmVaOwner.released == frames and owner_ref() is None
        assert ExportableAllocation.released == frames
        assert SyclEventOwner.released == frames
        assert dependency_registrations == frames
        source.close()
        raw = run_oracle(["ffmpeg", "-v", "error", "-hwaccel", "none", "-i", path,
                          "-fps_mode", "passthrough", "-f", "rawvideo", "-pix_fmt", "yuv420p", "pipe:1"]).stdout
        pixels = np.frombuffer(raw, np.uint8).reshape(frames, height * 3 // 2, width)
        mse = np.mean((pixels[:, :height].astype(np.float32) - reference)**2, axis=(1, 2))
        assert np.all(mse < 205.63), mse
        probe = run_oracle(["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_frames",
                            "-show_entries", "frame=best_effort_timestamp_time", "-of", "json", path])
        timestamps = [float(f["best_effort_timestamp_time"]) for f in json.loads(probe.stdout)["frames"]]
        assert len(timestamps) == frames
        assert all(abs(value - index / 30) <= .001 for index, value in enumerate(timestamps)), timestamps
    report = {"validation": "passed", "frames": frames, "device": identity, "layout": layout,
              "opencl_reuse_program": reuse,
              "dpctl": dpctl.__version__, "dpnp": dpnp.__version__, "owners_released": UsmVaOwner.released,
              "allocations_released": ExportableAllocation.released,
              "events_released": SyclEventOwner.released,
              "consumer_dependencies": dependency_registrations,
              "edges": {"decode_to_linear_usm": "gpu_materialization", "usm_to_dlpack": "pointer_identical",
                        "usm_to_va_encode": "shared_import"}, "public_api": True}
    Path(output).write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report))


if __name__ == "__main__":
    main()
