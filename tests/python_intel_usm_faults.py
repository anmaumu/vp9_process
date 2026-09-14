"""Qualify Intel USM provenance boundaries and producer-event failures.

This hardware-only test intentionally keeps allocation provenance validation in
the oneAPI adapter. The stable C ABI treats SYCL objects as opaque identities,
which is why Intel USM remains a v0.1 preview.
"""

from __future__ import annotations

import ctypes as ct
import json
import os
from pathlib import Path
import sys

native, extension, package, helper, output = sys.argv[1:6]
os.environ["MKVC_LIBRARY_PATH"] = native
sys.path[:0] = [package, extension]

import _dlpack  # noqa: E402
import dpctl  # noqa: E402
import mkvcodec  # noqa: E402
import mkvcodec.interop.dlpack as dlpack_api  # noqa: E402

dlpack_api.extension = _dlpack

P = ct.c_void_p
INT = ct.c_int
U64 = ct.c_uint64


def bind(library: ct.CDLL, name: str, *arguments: object) -> object:
    """Return a test-helper function with a checked ctypes signature."""
    function = getattr(library, name)
    function.restype = INT
    function.argtypes = list(arguments)
    return function


def main() -> None:
    """Probe context equivalence, cross-device rejection, and event failures."""
    output_path = Path(output)
    output_path.write_text('{"validation":"not_completed"}\n')
    library = ct.CDLL(helper)
    allocate = bind(library, "mkvc_test_sycl_alloc_exportable", P, U64, ct.POINTER(P))
    release = bind(library, "mkvc_test_sycl_free", P, P)
    export = bind(
        library,
        "mkvc_test_sycl_export_fd",
        P,
        P,
        ct.POINTER(INT),
        ct.POINTER(U64),
        ct.POINTER(U64),
    )
    create_event = bind(library, "mkvc_test_sycl_barrier_event", P, ct.POINTER(P), ct.POINTER(P))
    release_event = bind(library, "mkvc_test_sycl_event_free", P)

    producer = dpctl.SyclQueue("level_zero:gpu:0")
    same_device_queue = dpctl.SyclQueue("level_zero:gpu:0")
    other_device = dpctl.SyclQueue("level_zero:gpu:1")
    contexts_are_equivalent = producer.sycl_context == same_device_queue.sycl_context
    pointer = P()
    event_owner = P()
    event = P()
    frame = None
    if allocate(producer.addressof_ref(), 2 * 1024**2, ct.byref(pointer)) != 0:
        raise RuntimeError("failed to allocate exportable device USM")
    try:

        def probe_export(queue: object) -> int:
            """Validate allocation provenance and close any returned duplicate fd."""
            fd, size, offset = INT(-1), U64(), U64()
            result = export(
                queue.addressof_ref(),
                pointer,
                ct.byref(fd),
                ct.byref(size),
                ct.byref(offset),
            )
            if fd.value >= 0:
                os.close(fd.value)
            return int(result)

        if probe_export(producer) != 0:
            raise RuntimeError("same-context allocation validation failed")
        rejected_context = probe_export(same_device_queue) != 0
        rejected_device = probe_export(other_device) != 0
        context_result_valid = not rejected_context if contexts_are_equivalent else rejected_context
        if not context_result_valid or not rejected_device:
            raise RuntimeError(
                "oneAPI adapter accepted mismatched USM provenance: "
                f"contexts_are_equivalent={contexts_are_equivalent}, "
                f"cross_context_rejected={rejected_context}, "
                f"cross_device_rejected={rejected_device}"
            )

        if create_event(producer.addressof_ref(), ct.byref(event_owner), ct.byref(event)) != 0:
            raise RuntimeError("failed to create a Level Zero producer event")

        class Owner:
            pass

        frame = mkvcodec.GpuFrame.import_usm_nv12(
            pointer=pointer.value,
            context=producer.sycl_context.addressof_ref(),
            queue=producer.addressof_ref(),
            device_id=0,
            frame_size=(128, 128),
            pitch=128,
            owner=Owner(),
            event=event.value,
            dependency_registrar=lambda _event, _stream: (_ for _ in ()).throw(
                RuntimeError("injected dependency failure")
            ),
        )
        if frame.interop.api_stability != "preview":
            raise RuntimeError("Intel USM was not identified as preview")
        try:
            frame.plane(0).__dlpack__(stream=producer.addressof_ref())
        except RuntimeError as error:
            if str(error) != "injected dependency failure":
                raise
        else:
            raise RuntimeError("dependency registrar failure was ignored")
        frame.wait(5000)
        frame.close()
        frame = None
        report = {
            "validation": "passed",
            "producer_device": producer.sycl_device.name,
            "other_device": other_device.sycl_device.name,
            "same_context_export": "passed",
            "same_device_context": (
                "equivalent_native_context"
                if contexts_are_equivalent
                else "distinct_context_rejected_by_oneapi_adapter"
            ),
            "cross_device": "rejected_by_oneapi_adapter",
            "dependency_failure": "propagated",
            "core_provenance": "opaque_caller_contract",
            "api_stability": "preview",
        }
        output_path.write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps(report, sort_keys=True))
    finally:
        if frame is not None:
            frame.close()
        if event_owner.value:
            release_event(event_owner)
        if pointer.value:
            release(producer.addressof_ref(), pointer)


if __name__ == "__main__":
    main()
