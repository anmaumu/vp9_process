"""Inspect decoder/externally allocated VA layout without mapping pixel buffers."""
import json
import os
import sys
from pathlib import Path

native, extension, package, fixture, output = sys.argv[1:6]
os.environ["MKVC_LIBRARY_PATH"] = native
sys.path[:0] = [package, extension]
import mkvcodec
from intel_va_opencl_support import VaOwner, invert_luma
from intel_va_prime_support import export_layout

Path(output).write_text('{"validation":"not_completed"}\n')
report = {"validation": "not_completed", "usm_dlpack_implemented": False, "layouts": {}}
with mkvcodec.VideoCapture(fixture, backend="intel", prefetch=0, require_gpu_resident=True) as capture:
    source = capture.read_surface()
    source.wait(5000)
    native = source.native_handle["handles"]
    desc = source.descriptor
    report["layouts"]["decoder"] = export_layout(native[0], native[1])
    for linear in (False, True):
        name = "linear_requested" if linear else "default_external"
        owner = None
        try:
            owner = VaOwner(source, desc["width"], desc["height"], linear=linear)
            report["layouts"][name] = export_layout(owner.display, owner.surface.value)
            report["device"] = invert_luma(source, owner, desc["width"], desc["height"])
        except RuntimeError as error:
            report["layouts"][name] = {"unsupported": str(error)}
        finally:
            if owner:
                owner.close()
    source.close()
report["validation"] = "observed"  # Not a USM capability advertisement.
Path(output).write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps(report, sort_keys=True))
