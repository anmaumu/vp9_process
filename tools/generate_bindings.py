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
PYTHON_TYPES = ROOT / "python" / "mkvcodec" / "_native_types.py"
DOTNET_NATIVE = ROOT / "dotnet" / "MkvCodec" / "NativeMethods.cs"
DOTNET_METHODS = ROOT / "dotnet" / "MkvCodec" / "NativeMethods.Generated.cs"
DOTNET_TYPES = ROOT / "dotnet" / "MkvCodec" / "NativeTypes.Generated.cs"
START_MARKER = "# BEGIN MKVC GENERATED CTYPES SIGNATURES"
END_MARKER = "# END MKVC GENERATED CTYPES SIGNATURES"
DOTNET_START_MARKER = "    // BEGIN MKVC GENERATED PINVOKE DECLARATIONS"
DOTNET_END_MARKER = "    // END MKVC GENERATED PINVOKE DECLARATIONS"

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
_PYTHON_FIELD_TYPES = {
    "char*": "ct.c_char_p",
    "const char*": "ct.c_char_p",
    "int32_t": "ct.c_int32",
    "int64_t": "ct.c_int64",
    "uint8_t": "ct.c_uint8",
    "uint32_t": "ct.c_uint32",
    "uint64_t": "ct.c_uint64",
    "void*": "ct.c_void_p",
    "mkvc_gpu_external_query_callback": "ct.c_void_p",
    "mkvc_gpu_external_release_callback": "ct.c_void_p",
}
_OPAQUE_TYPES = {
    "mkvc_cpu_buffer", "mkvc_cpu_frame_pool", "mkvc_decoder", "mkvc_encoder",
    "mkvc_frame", "mkvc_gpu_frame", "mkvc_gpu_resource_pool",
    "mkvc_gpu_resource_reservation", "mkvc_submission",
}
_DOTNET_TYPES = {
    "mkvc_backend_capability": "MkvBackendCapability",
    "mkvc_copy_policy": "NativeCopyPolicy",
    "mkvc_cpu_buffer_desc": "MkvCpuBufferDescriptor",
    "mkvc_cpu_frame_pool_config": "NativeCpuFramePoolConfig",
    "mkvc_decoder_config": "NativeDecoderConfig",
    "mkvc_encoder_config": "NativeEncoderConfig",
    "mkvc_frame_process_config": "NativeFrameProcessConfig",
    "mkvc_frame_view": "NativeFrameView",
    "mkvc_gpu_external_frame_config": "NativeGpuExternalFrameConfig",
    "mkvc_gpu_frame_desc": "MkvGpuFrameDescriptor",
    "mkvc_gpu_native_handle_desc": "MkvGpuNativeHandleDescriptor",
    "mkvc_gpu_resource_pool_config": "NativeGpuResourcePoolConfig",
    "mkvc_gpu_resource_pool_stats": "MkvGpuResourcePoolStatistics",
    "mkvc_gpu_resource_reservation_desc": "MkvGpuResourceReservationDescriptor",
    "mkvc_mutable_frame_view": "NativeMutableFrameView",
    "mkvc_pipeline_metrics": "MkvPipelineMetrics",
    "mkvc_version": "MkvVersion",
}
_DOTNET_ENUMS = {
    "mkvc_result": ("MkvResult", {
        "MKVC_OK": "Ok",
        "MKVC_ERROR_INVALID_ARGUMENT": "InvalidArgument",
        "MKVC_ERROR_BUFFER_TOO_SMALL": "BufferTooSmall",
        "MKVC_ERROR_NOT_SUPPORTED": "NotSupported",
        "MKVC_ERROR_INTERNAL": "Internal",
        "MKVC_ERROR_INVALID_STATE": "InvalidState",
        "MKVC_ERROR_IO": "Io",
        "MKVC_ERROR_CODEC": "Codec",
        "MKVC_END_OF_STREAM": "EndOfStream",
        "MKVC_WOULD_BLOCK": "WouldBlock",
        "MKVC_ERROR_TIMEOUT": "Timeout",
        "MKVC_ERROR_CANCELLED": "Cancelled",
    }),
    "mkvc_backend": ("MkvBackend", {
        "MKVC_BACKEND_CPU": "Cpu",
        "MKVC_BACKEND_NVIDIA": "Nvidia",
        "MKVC_BACKEND_INTEL": "Intel",
    }),
    "mkvc_codec": ("MkvCodecKind", {
        "MKVC_CODEC_VP9": "Vp9",
        "MKVC_CODEC_AV1": "Av1",
    }),
    "mkvc_pixel_format": ("MkvPixelFormat", {
        "MKVC_PIXEL_FORMAT_I420": "I420",
        "MKVC_PIXEL_FORMAT_NV12": "Nv12",
        "MKVC_PIXEL_FORMAT_BGR24": "Bgr24",
        "MKVC_PIXEL_FORMAT_RGB24": "Rgb24",
        "MKVC_PIXEL_FORMAT_BGRA32": "Bgra32",
        "MKVC_PIXEL_FORMAT_P010": "P010",
    }),
    "mkvc_gpu_memory_type": ("MkvGpuMemoryType", {
        "MKVC_GPU_MEMORY_D3D11_TEXTURE": "D3D11Texture",
        "MKVC_GPU_MEMORY_VA_SURFACE": "VaSurface",
        "MKVC_GPU_MEMORY_CUDA_POINTER": "CudaPointer",
        "MKVC_GPU_MEMORY_CUDA_ARRAY": "CudaArray",
        "MKVC_GPU_MEMORY_USM": "Usm",
    }),
    "mkvc_gpu_native_handle_type": ("MkvGpuNativeHandleType", {
        "MKVC_GPU_NATIVE_D3D11_TEXTURE": "D3D11Texture",
        "MKVC_GPU_NATIVE_VA_SURFACE": "VaSurface",
        "MKVC_GPU_NATIVE_CUDA_POINTER": "CudaPointer",
        "MKVC_GPU_NATIVE_CUDA_ARRAY": "CudaArray",
        "MKVC_GPU_NATIVE_USM_POINTER": "UsmPointer",
    }),
    "mkvc_submission_status": ("MkvSubmissionStatus", {
        "MKVC_SUBMISSION_PENDING": "Pending",
        "MKVC_SUBMISSION_COMPLETE": "Complete",
        "MKVC_SUBMISSION_FAILED": "Failed",
        "MKVC_SUBMISSION_CANCELLED": "Cancelled",
    }),
}
_DOTNET_PUBLIC_STRUCTS = {
    "mkvc_backend_capability", "mkvc_cpu_buffer_desc", "mkvc_gpu_frame_desc",
    "mkvc_gpu_native_handle_desc", "mkvc_gpu_resource_pool_stats",
    "mkvc_gpu_resource_reservation_desc", "mkvc_pipeline_metrics", "mkvc_version",
}
_DOTNET_FIELD_TYPES = {
    ("mkvc_backend_capability", "backend"): "MkvBackend",
    ("mkvc_backend_capability", "codec"): "MkvCodecKind",
    ("mkvc_cpu_buffer_desc", "pixel_format"): "MkvPixelFormat",
    ("mkvc_cpu_frame_pool_config", "pixel_format"): "MkvPixelFormat",
    ("mkvc_frame_process_config", "backend"): "MkvBackend",
    ("mkvc_gpu_frame_desc", "backend"): "MkvBackend",
    ("mkvc_gpu_frame_desc", "memory_type"): "MkvGpuMemoryType",
    ("mkvc_gpu_frame_desc", "pixel_format"): "MkvPixelFormat",
    ("mkvc_gpu_native_handle_desc", "type"): "MkvGpuNativeHandleType",
    ("mkvc_mutable_frame_view", "pixel_format"): "MkvPixelFormat",
}
_DOTNET_FIELD_NAMES = {
    "backend_time_ns": "BackendTimeNanoseconds",
    "queue_wait_ns": "QueueWaitNanoseconds",
    "wait_ns": "WaitNanoseconds",
}
_DOTNET_HANDLES = {
    "mkvc_cpu_buffer": "MkvCpuBufferHandle",
    "mkvc_cpu_frame_pool": "MkvCpuFramePoolHandle",
    "mkvc_decoder": "MkvDecoderHandle",
    "mkvc_encoder": "MkvEncoderHandle",
    "mkvc_frame": "MkvFrameHandle",
    "mkvc_gpu_frame": "MkvGpuFrameHandle",
    "mkvc_gpu_resource_pool": "MkvGpuResourcePoolHandle",
    "mkvc_gpu_resource_reservation": "MkvGpuResourceReservationHandle",
    "mkvc_submission": "MkvSubmissionHandle",
}
_DOTNET_RAW_HANDLES = {
    "mkvc_cpu_buffer_release", "mkvc_cpu_frame_pool_destroy",
    "mkvc_decoder_close", "mkvc_decoder_destroy", "mkvc_decoder_get_metrics",
    "mkvc_encoder_close", "mkvc_encoder_destroy", "mkvc_encoder_get_metrics",
    "mkvc_frame_release", "mkvc_gpu_frame_release",
    "mkvc_gpu_resource_pool_destroy", "mkvc_gpu_resource_reservation_release",
    "mkvc_submission_release",
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


def _argument_parts(argument: str) -> tuple[str, str]:
    match = re.fullmatch(r"(.+?[*\s])\s*(\w+)", argument)
    if match is None:
        raise BindingGenerationError(f"invalid function argument: {argument}")
    return match.group(1).strip(), match.group(2)


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


def _struct_field(field: str) -> tuple[str, str, int | None]:
    match = re.fullmatch(r"(.+?[*\s])\s*(\w+)(?:\[(\d+)\])?", field)
    if match is None:
        raise BindingGenerationError(f"invalid C ABI struct field: {field}")
    return " ".join(match.group(1).split()), match.group(2), (
        int(match.group(3)) if match.group(3) else None
    )


def _python_field_type(c_type: str, array_size: int | None) -> str:
    normalized = " ".join(c_type.split())
    pointer_depth = normalized.count("*")
    base = normalized.replace("const", "").replace("*", "").strip()
    if normalized in _PYTHON_FIELD_TYPES:
        result = _PYTHON_FIELD_TYPES[normalized]
    elif pointer_depth == 1 and base == "uint8_t":
        result = "ct.POINTER(ct.c_uint8)"
    elif pointer_depth == 0 and base in _PYTHON_FIELD_TYPES:
        result = _PYTHON_FIELD_TYPES[base]
    elif pointer_depth == 0 and base in _PYTHON_TYPES:
        result = _PYTHON_TYPES[base]
    else:
        raise BindingGenerationError(f"unmapped C ABI struct field type: {c_type}")
    if array_size is not None:
        result = f"{result} * {array_size}"
    return result


def render_python_types(header: Path = abi_guard.HEADER) -> str:
    """Render ctypes constants and structures from the canonical C ABI."""
    surface = abi_guard._surface(header)
    enums = surface["enums"]
    pending = dict(surface["structs"])
    ordered: list[tuple[str, list[str]]] = []
    while pending:
        progressed = False
        for name, fields in list(pending.items()):
            dependencies = {
                _struct_field(field)[0].replace("const", "").replace("*", "").strip()
                for field in fields
            } & set(pending)
            if dependencies:
                continue
            ordered.append((name, fields))
            del pending[name]
            progressed = True
        if not progressed:
            raise BindingGenerationError(
                "cyclic or unresolved C ABI struct dependencies: "
                + ", ".join(sorted(pending))
            )
    exported = ["MKVC_ABI_VERSION"]
    lines = [
        '"""Generated ctypes constants and structures; do not edit."""',
        "",
        "from __future__ import annotations",
        "",
        "import ctypes as ct",
        "",
        f'MKVC_ABI_VERSION = {surface["abi_version"]}',
    ]
    for values in enums.values():
        for name, value in values.items():
            lines.append(f"{name} = {value}")
            exported.append(name)
    for struct_name, fields in ordered:
        class_name = _PYTHON_TYPES.get(struct_name)
        if class_name is None:
            raise BindingGenerationError(f"unmapped Python ABI struct: {struct_name}")
        exported.append(class_name)
        lines.extend(("", "", f"class {class_name}(ct.Structure):", "    _fields_ = ["))
        for field in fields:
            c_type, name, array_size = _struct_field(field)
            lines.append(
                f'        ("{name}", {_python_field_type(c_type, array_size)}),'
            )
        lines.append("    ]")
    lines.extend(("", "", "__all__ = ["))
    lines.extend(f'    "{name}",' for name in exported)
    lines.append("]")
    return "\n".join(lines) + "\n"


def _pascal_case(name: str) -> str:
    return "".join(part[:1].upper() + part[1:] for part in name.split("_"))


def _dotnet_field_name(struct_name: str, field_name: str) -> str:
    if field_name == "pts" and struct_name == "mkvc_gpu_frame_desc":
        return "PtsNanoseconds"
    return _DOTNET_FIELD_NAMES.get(field_name, _pascal_case(field_name))


def _dotnet_struct_scalar_type(
    struct_name: str, c_type: str, field_name: str
) -> str:
    override = _DOTNET_FIELD_TYPES.get((struct_name, field_name))
    if override is not None:
        return override
    normalized = " ".join(c_type.split())
    values = {
        "int32_t": "int",
        "int64_t": "long",
        "uint8_t": "byte",
        "uint32_t": "uint",
        "uint64_t": "ulong",
        "void*": "nint",
        "const char*": "nint",
        "mkvc_gpu_external_query_callback": "nint",
        "mkvc_gpu_external_release_callback": "nint",
    }
    if normalized in values:
        return values[normalized]
    if normalized in _DOTNET_TYPES:
        return _DOTNET_TYPES[normalized]
    raise BindingGenerationError(f"unmapped .NET ABI struct field type: {c_type}")


def _render_dotnet_struct_field(
    struct_name: str, field: str, access: str
) -> list[str]:
    c_type, field_name, array_size = _struct_field(field)
    name = _dotnet_field_name(struct_name, field_name)
    if array_size is None:
        return [
            f"    {access} {_dotnet_struct_scalar_type(struct_name, c_type, field_name)} {name};"
        ]
    normalized = " ".join(c_type.split())
    if field_name == "reserved" and normalized == "uint8_t" and array_size == 2:
        return [f"    {access} ushort {name};"]
    if field_name in ("planes", "strides") and array_size == 4:
        element_type = "nint" if "*" in normalized else "int"
        singular = "Plane" if field_name == "planes" else "Stride"
        return [f"    {access} {element_type} {singular}{index};" for index in range(4)]
    element_type = _dotnet_struct_scalar_type(struct_name, c_type, field_name)
    return [
        f"    [MarshalAs(UnmanagedType.ByValArray, SizeConst = {array_size})]",
        f"    {access} {element_type}[] {name};",
    ]


def render_dotnet_types(header: Path = abi_guard.HEADER) -> str:
    """Render managed enums and structures without changing the public C# surface."""
    surface = abi_guard._surface(header)
    lines = [
        "// Generated from include/mkvcodec/mkvc.h; do not edit.",
        "using System.Runtime.InteropServices;",
        "",
        "namespace MkvCodec;",
    ]
    for c_name, (managed_name, members) in _DOTNET_ENUMS.items():
        values = surface["enums"].get(c_name)
        if values is None:
            raise BindingGenerationError(f"missing C ABI enum: {c_name}")
        if set(values) != set(members):
            raise BindingGenerationError(f"unmapped .NET ABI enum member: {c_name}")
        lines.extend(("", f"public enum {managed_name} : uint", "{"))
        lines.extend(f"    {members[name]} = {value}," for name, value in values.items())
        lines.append("}")
    for c_name, fields in surface["structs"].items():
        managed_name = _DOTNET_TYPES.get(c_name)
        if managed_name is None:
            raise BindingGenerationError(f"unmapped .NET ABI struct: {c_name}")
        access = "public" if c_name in _DOTNET_PUBLIC_STRUCTS else "internal"
        lines.extend(("", "[StructLayout(LayoutKind.Sequential)]", f"{access} struct {managed_name}", "{"))
        for field in fields:
            lines.extend(_render_dotnet_struct_field(c_name, field, access))
        lines.append("}")
    return "\n".join(lines) + "\n"


def _dotnet_return(c_type: str) -> str:
    normalized = " ".join(c_type.split())
    values = {"mkvc_result": "MkvResult", "void": "void", "const char*": "nint"}
    if normalized not in values:
        raise BindingGenerationError(f"unmapped .NET return type: {c_type}")
    return values[normalized]


def _dotnet_argument(function: str, c_type: str, name: str) -> str:
    is_const = "const" in c_type.split()
    normalized = " ".join(c_type.replace("const", "").split())
    pointer_depth = normalized.count("*")
    base = normalized.replace("*", "").strip()
    if base in _DOTNET_HANDLES:
        if pointer_depth == 2:
            return f"out {_DOTNET_HANDLES[base]} {name}"
        if pointer_depth == 1:
            argument_type = "nint" if function in _DOTNET_RAW_HANDLES else _DOTNET_HANDLES[base]
            return f"{argument_type} {name}"
    if base in _DOTNET_TYPES and pointer_depth == 1:
        if function == "mkvc_get_backend_capabilities":
            return f"nint {name}"
        return f"ref {_DOTNET_TYPES[base]} {name}"
    if base == "void" and pointer_depth == 1:
        return f"nint {name}"
    if base == "void" and pointer_depth == 2:
        return f"out nint {name}"
    if base == "size_t" and pointer_depth == 1:
        return f"ref nuint {name}"
    if base == "uint32_t" and pointer_depth == 1:
        if function == "mkvc_submission_query":
            return f"out MkvSubmissionStatus {name}"
        return f"out uint {name}"
    scalars = {
        "int64_t": "long", "mkvc_gpu_dependency_callback": "nint",
        "mkvc_result": "MkvResult", "uint32_t": "uint", "uint64_t": "ulong",
    }
    if pointer_depth == 0 and base in scalars:
        return f"{scalars[base]} {name}"
    qualifier = "const " if is_const else ""
    raise BindingGenerationError(
        f"unmapped .NET argument type: {qualifier}{normalized} {name}")


def render_dotnet(header: Path = abi_guard.HEADER) -> str:
    """Render all P/Invoke methods from the canonical C ABI header."""
    functions = abi_guard._surface(header)["functions"]
    lines = [
        "// <auto-generated />",
        "using System.Runtime.InteropServices;",
        "",
        "namespace MkvCodec;",
        "",
        "internal static partial class NativeMethods",
        "{",
    ]
    for signature in functions.values():
        return_type, name, arguments = _split_signature(signature)
        rendered = [
            _dotnet_argument(name, *_argument_parts(argument))
            for argument in arguments]
        lines.extend((
            "    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]",
        ))
        declaration = f"    internal static extern {_dotnet_return(return_type)} {name}"
        if not rendered:
            lines.extend((declaration + "();", ""))
            continue
        lines.append(declaration + "(")
        lines.extend(
            f"        {argument}{',' if index + 1 < len(rendered) else ');'}"
            for index, argument in enumerate(rendered))
        lines.append("")
    lines.append("}")
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


def _dotnet_loader(source: str) -> str:
    pattern = re.compile(
        re.escape(DOTNET_START_MARKER) + r".*?" +
        re.escape(DOTNET_END_MARKER), re.DOTALL)
    replacement = DOTNET_START_MARKER + "\n" + DOTNET_END_MARKER
    rewritten, count = pattern.subn(replacement, source)
    if count != 1:
        raise BindingGenerationError("P/Invoke generated-region markers are invalid")
    return rewritten


def generate() -> None:
    """Write deterministic Python binding artifacts."""
    PYTHON_SIGNATURES.write_text(render_python(), encoding="utf-8")
    PYTHON_TYPES.write_text(render_python_types(), encoding="utf-8")
    source = PYTHON_NATIVE.read_text(encoding="utf-8")
    PYTHON_NATIVE.write_text(_native_loader(source), encoding="utf-8")
    DOTNET_METHODS.write_text(render_dotnet(), encoding="utf-8")
    DOTNET_TYPES.write_text(render_dotnet_types(), encoding="utf-8")
    dotnet_source = DOTNET_NATIVE.read_text(encoding="utf-8")
    DOTNET_NATIVE.write_text(_dotnet_loader(dotnet_source), encoding="utf-8")


def check() -> None:
    """Fail when checked-in generated artifacts differ from canonical output."""
    if not PYTHON_SIGNATURES.exists() or PYTHON_SIGNATURES.read_text(
            encoding="utf-8") != render_python():
        raise BindingGenerationError(
            "generated Python signatures are stale; run tools/generate_bindings.py generate")
    if not PYTHON_TYPES.exists() or PYTHON_TYPES.read_text(
            encoding="utf-8") != render_python_types():
        raise BindingGenerationError(
            "generated Python types are stale; run tools/generate_bindings.py generate")
    source = PYTHON_NATIVE.read_text(encoding="utf-8")
    if source != _native_loader(source):
        raise BindingGenerationError("_native.py contains hand-edited generated declarations")
    if not DOTNET_METHODS.exists() or DOTNET_METHODS.read_text(
            encoding="utf-8") != render_dotnet():
        raise BindingGenerationError(
            "generated .NET methods are stale; run tools/generate_bindings.py generate")
    if not DOTNET_TYPES.exists() or DOTNET_TYPES.read_text(
            encoding="utf-8") != render_dotnet_types():
        raise BindingGenerationError(
            "generated .NET types are stale; run tools/generate_bindings.py generate")
    dotnet_source = DOTNET_NATIVE.read_text(encoding="utf-8")
    if dotnet_source != _dotnet_loader(dotnet_source):
        raise BindingGenerationError("NativeMethods.cs contains generated declarations")


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
