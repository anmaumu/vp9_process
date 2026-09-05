#!/usr/bin/env python3
"""Generate low-level language bindings from the canonical public C header."""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

import abi_guard

ROOT = Path(__file__).resolve().parents[1]
PYTHON_NATIVE = ROOT / "python" / "mkvcodec" / "_native.py"
PYTHON_SIGNATURES = ROOT / "python" / "mkvcodec" / "_native_signatures.py"
START_MARKER = "# BEGIN MKVC GENERATED CTYPES SIGNATURES"
END_MARKER = "# END MKVC GENERATED CTYPES SIGNATURES"

_PYTHON_TYPES = {
    "mkvc_backend_capability": "BackendCapability",
    "mkvc_copy_policy": "CopyPolicy",
    "mkvc_cpu_buffer": "CpuBufferHandle",
    "mkvc_cpu_buffer_desc": "CpuBufferDesc",
    "mkvc_cpu_frame_pool": "CpuFramePoolHandle",
    "mkvc_cpu_frame_pool_config": "CpuFramePoolConfig",
    "mkvc_decoder": "DecoderHandle",
    "mkvc_decoder_config": "DecoderConfig",
    "mkvc_encoder": "EncoderHandle",
    "mkvc_encoder_config": "EncoderConfig",
    "mkvc_frame": "FrameHandle",
    "mkvc_frame_process_config": "FrameProcessConfig",
    "mkvc_frame_view": "FrameView",
    "mkvc_gpu_external_frame_config": "GpuExternalFrameConfig",
    "mkvc_gpu_frame": "GpuFrameHandle",
    "mkvc_gpu_frame_desc": "GpuFrameDesc",
    "mkvc_gpu_native_handle_desc": "GpuNativeHandleDesc",
    "mkvc_gpu_resource_pool": "GpuResourcePoolHandle",
    "mkvc_gpu_resource_pool_config": "GpuResourcePoolConfig",
    "mkvc_gpu_resource_pool_stats": "GpuResourcePoolStats",
    "mkvc_gpu_resource_reservation": "GpuResourceReservationHandle",
    "mkvc_gpu_resource_reservation_desc": "GpuResourceReservationDesc",
    "mkvc_mutable_frame_view": "MutableFrameView",
    "mkvc_pipeline_metrics": "PipelineMetrics",
    "mkvc_submission": "SubmissionHandle",
    "mkvc_version": "Version",
}
_CTYPES = {
    "int64_t": "ct.c_int64",
    "mkvc_gpu_dependency_callback": 't["GpuDependencyCallback"]',
    "mkvc_result": "ct.c_int",
    "size_t": "ct.c_size_t",
    "uint32_t": "ct.c_uint32",
    "uint64_t": "ct.c_uint64",
    "void": "None",
}
_OPAQUE_TYPES = {
    "mkvc_cpu_buffer", "mkvc_cpu_frame_pool", "mkvc_decoder", "mkvc_encoder",
    "mkvc_frame", "mkvc_gpu_frame", "mkvc_gpu_resource_pool",
    "mkvc_gpu_resource_reservation", "mkvc_submission",
}


class BindingGenerationError(RuntimeError):
    """Raised when a C declaration cannot be represented safely."""


def _split_signature(signature: str) -> tuple[str, str, list[str]]:
    match = re.fullmatch(r"(.+?)\s+(mkvc_\w+)\((.*)\)", signature)
    if match is None:
        raise BindingGenerationError(f"invalid function signature: {signature}")
    arguments = [] if match.group(3).strip() in ("", "void") else [
        value.strip() for value in match.group(3).split(",")]
    return match.group(1), match.group(2), arguments


def _argument_type(argument: str) -> str:
    match = re.fullmatch(r"(.+?[*\s])\s*(\w+)", argument)
    if match is None:
        raise BindingGenerationError(f"invalid function argument: {argument}")
    return match.group(1).strip()


def _ctypes_type(c_type: str) -> str:
    normalized = " ".join(c_type.replace("const", "").split())
    pointer_depth = normalized.count("*")
    base = normalized.replace("*", "").strip()
    if base == "char" and pointer_depth == 1:
        return "ct.c_char_p"
    if base == "void" and pointer_depth == 1:
        return "ct.c_void_p"
    if base == "void" and pointer_depth == 2:
        return "ct.POINTER(ct.c_void_p)"
    if base in _PYTHON_TYPES:
        expression = f't["{_PYTHON_TYPES[base]}"]'
    elif base in _CTYPES:
        expression = _CTYPES[base]
    else:
        raise BindingGenerationError(f"unmapped C ABI type: {c_type}")
    if base in _OPAQUE_TYPES:
        if pointer_depth == 0:
            raise BindingGenerationError(f"opaque ABI type is not a pointer: {c_type}")
        pointer_depth -= 1
    for _ in range(pointer_depth):
        expression = f"ct.POINTER({expression})"
    return expression


def render_python(header: Path = abi_guard.HEADER) -> str:
    """Render the generated ctypes configuration module.

    Parameters
    ----------
    header : pathlib.Path
        Canonical C ABI header.

    Returns
    -------
    str
        Complete generated Python module contents.
    """
    functions = abi_guard._surface(header)["functions"]
    lines = [
        '"""Generated ctypes signatures; run tools/generate_bindings.py."""',
        "",
        "from __future__ import annotations",
        "",
        "import ctypes as ct",
        "from typing import Any",
        "",
        "",
        "def configure(lib: ct.CDLL, t: dict[str, Any]) -> None:",
        '    """Apply generated argument and result types to the loaded ABI.',
        "",
        "    Parameters",
        "    ----------",
        "    lib : ctypes.CDLL",
        "        Loaded mkvcodec shared library.",
        "    t : dict of str to Any",
        "        Native module namespace containing generated type dependencies.",
        '    """',
    ]
    for signature in functions.values():
        return_type, name, arguments = _split_signature(signature)
        rendered_arguments = [
            _ctypes_type(_argument_type(argument)) for argument in arguments]
        single_line = f"    lib.{name}.argtypes = [{', '.join(rendered_arguments)}]"
        if len(single_line) <= 99:
            lines.append(single_line)
        else:
            lines.append(f"    lib.{name}.argtypes = [")
            lines.extend(f"        {argument}," for argument in rendered_arguments)
            lines.append("    ]")
        lines.append(f"    lib.{name}.restype = {_ctypes_type(return_type)}")
    return "\n".join(lines) + "\n"


def _native_loader(source: str) -> str:
    replacement = "\n".join((
        START_MARKER,
        "from ._native_signatures import configure as _configure_signatures",
        "_configure_signatures(lib, globals())",
        "del _configure_signatures",
        END_MARKER,
    ))
    pattern = re.compile(
        re.escape(START_MARKER) + r".*?" + re.escape(END_MARKER), re.DOTALL)
    rewritten, count = pattern.subn(replacement, source)
    if count != 1:
        raise BindingGenerationError("ctypes generated-region markers are invalid")
    return rewritten


def generate() -> None:
    """Write deterministic Python binding artifacts."""
    PYTHON_SIGNATURES.write_text(render_python(), encoding="utf-8")
    source = PYTHON_NATIVE.read_text(encoding="utf-8")
    PYTHON_NATIVE.write_text(_native_loader(source), encoding="utf-8")


def check() -> None:
    """Fail when checked-in generated artifacts differ from canonical output."""
    if not PYTHON_SIGNATURES.exists() or PYTHON_SIGNATURES.read_text(
            encoding="utf-8") != render_python():
        raise BindingGenerationError(
            "generated Python signatures are stale; run tools/generate_bindings.py generate")
    source = PYTHON_NATIVE.read_text(encoding="utf-8")
    if source != _native_loader(source):
        raise BindingGenerationError("_native.py contains hand-edited generated declarations")


def main() -> int:
    """Generate binding files or verify their reproducibility."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("generate", "check"))
    args = parser.parse_args()
    try:
        generate() if args.command == "generate" else check()
    except (BindingGenerationError, abi_guard.AbiGuardError, OSError) as error:
        print(f"binding-generation: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
