from __future__ import annotations

import ctypes as ct
import ctypes.util
import os
from pathlib import Path

MKVC_OK = 0
MKVC_ERROR_INTERNAL = 4
MKVC_ERROR_INVALID_STATE = 5
MKVC_ERROR_TIMEOUT = 10
MKVC_ERROR_CANCELLED = 11
MKVC_END_OF_STREAM = 8
MKVC_WOULD_BLOCK = 9
MKVC_SUBMISSION_PENDING = 0
MKVC_SUBMISSION_COMPLETE = 1
MKVC_SUBMISSION_FAILED = 2
MKVC_SUBMISSION_CANCELLED = 3
MKVC_BACKEND_CPU = 1
MKVC_BACKEND_NVIDIA = 2
MKVC_BACKEND_INTEL = 3
MKVC_CODEC_VP9 = 1
MKVC_CODEC_AV1 = 2
MKVC_PIXEL_FORMAT_I420 = 1
MKVC_PIXEL_FORMAT_NV12 = 2
MKVC_PIXEL_FORMAT_BGR24 = 3
MKVC_PIXEL_FORMAT_RGB24 = 4
MKVC_PIXEL_FORMAT_BGRA32 = 5
MKVC_PIXEL_FORMAT_P010 = 6
MKVC_GPU_MEMORY_CUDA_POINTER = 3
MKVC_GPU_MEMORY_CUDA_ARRAY = 4
MKVC_GPU_MEMORY_VA_SURFACE = 2
MKVC_GPU_MEMORY_D3D11_TEXTURE = 1
MKVC_GPU_MEMORY_USM = 5
MKVC_GPU_NATIVE_D3D11_TEXTURE = 1
MKVC_GPU_NATIVE_VA_SURFACE = 2
MKVC_GPU_NATIVE_CUDA_POINTER = 3
MKVC_GPU_NATIVE_CUDA_ARRAY = 4
MKVC_GPU_NATIVE_USM_POINTER = 5
MKVC_FRAME_FIT_STRETCH = 0
MKVC_FRAME_FIT_CONTAIN = 1
MKVC_FRAME_FIT_COVER = 2


class EncoderConfig(ct.Structure):
    _fields_ = [
        ("struct_size", ct.c_uint32),
        ("struct_version", ct.c_uint32),
        ("output_path_utf8", ct.c_char_p),
        ("codec", ct.c_uint32),
        ("backend", ct.c_uint32),
        ("width", ct.c_uint32),
        ("height", ct.c_uint32),
        ("fps_num", ct.c_uint32),
        ("fps_den", ct.c_uint32),
        ("quality", ct.c_uint32),
        ("keyframe_interval_frames", ct.c_uint32),
        ("threads", ct.c_uint32),
        ("queue_size", ct.c_uint32),
    ]


class BackendCapability(ct.Structure):
    _fields_ = [
        ("struct_size", ct.c_uint32), ("backend", ct.c_uint32),
        ("codec", ct.c_uint32), ("can_decode", ct.c_uint8),
        ("can_encode", ct.c_uint8), ("is_hardware", ct.c_uint8),
        ("reserved", ct.c_uint8),
    ]


class DecoderConfig(ct.Structure):
    _fields_ = [
        ("struct_size", ct.c_uint32),
        ("struct_version", ct.c_uint32),
        ("input_path_utf8", ct.c_char_p),
        ("codec", ct.c_uint32),
        ("backend", ct.c_uint32),
        ("threads", ct.c_uint32),
        ("prefetch", ct.c_uint32),
    ]


class FrameView(ct.Structure):
    _fields_ = [
        ("struct_size", ct.c_uint32),
        ("struct_version", ct.c_uint32),
        ("pixel_format", ct.c_uint32),
        ("width", ct.c_uint32),
        ("height", ct.c_uint32),
        ("planes", ct.POINTER(ct.c_uint8) * 4),
        ("strides", ct.c_int32 * 4),
        ("pts", ct.c_int64),
    ]


class MutableFrameView(ct.Structure):
    _fields_ = [
        ("struct_size", ct.c_uint32),
        ("struct_version", ct.c_uint32),
        ("pixel_format", ct.c_uint32),
        ("width", ct.c_uint32),
        ("height", ct.c_uint32),
        ("planes", ct.POINTER(ct.c_uint8) * 4),
        ("strides", ct.c_int32 * 4),
        ("pts", ct.c_int64),
    ]


class CpuFramePoolConfig(ct.Structure):
    _fields_ = [
        ("struct_size", ct.c_uint32),
        ("struct_version", ct.c_uint32),
        ("pixel_format", ct.c_uint32),
        ("width", ct.c_uint32),
        ("height", ct.c_uint32),
        ("capacity", ct.c_uint32),
    ]


class CpuBufferDesc(ct.Structure):
    _fields_ = [
        ("struct_size", ct.c_uint32),
        ("struct_version", ct.c_uint32),
        ("pixel_format", ct.c_uint32),
        ("width", ct.c_uint32),
        ("height", ct.c_uint32),
        ("plane_count", ct.c_uint32),
        ("generation", ct.c_uint64),
    ]


class GpuResourcePoolConfig(ct.Structure):
    _fields_ = [
        ("struct_size", ct.c_uint32), ("struct_version", ct.c_uint32),
        ("capacity", ct.c_uint32), ("reserved", ct.c_uint32),
    ]


class GpuResourceReservationDesc(ct.Structure):
    _fields_ = [
        ("struct_size", ct.c_uint32), ("struct_version", ct.c_uint32),
        ("slot_index", ct.c_uint32), ("reserved", ct.c_uint32),
        ("generation", ct.c_uint64),
    ]


class GpuResourcePoolStats(ct.Structure):
    _fields_ = [
        ("struct_size", ct.c_uint32), ("struct_version", ct.c_uint32),
        ("capacity", ct.c_uint32), ("in_use", ct.c_uint32),
        ("peak_in_use", ct.c_uint32), ("reserved", ct.c_uint32),
        ("acquisitions", ct.c_uint64),
        ("rejected_acquisitions", ct.c_uint64), ("wait_ns", ct.c_uint64),
    ]


class PipelineMetrics(ct.Structure):
    _fields_ = [
        ("struct_size", ct.c_uint32),
        ("struct_version", ct.c_uint32),
        ("accepted_frames", ct.c_uint64),
        ("completed_frames", ct.c_uint64),
        ("rejected_frames", ct.c_uint64),
        ("queue_wait_ns", ct.c_uint64),
        ("backend_time_ns", ct.c_uint64),
        ("queue_capacity", ct.c_uint32),
        ("peak_queue_depth", ct.c_uint32),
        ("hardware_pending_peak", ct.c_uint32),
        ("copy_path", ct.c_uint32),
    ]


class CopyPolicy(ct.Structure):
    _fields_ = [
        ("struct_size", ct.c_uint32),
        ("struct_version", ct.c_uint32),
        ("require_gpu_resident", ct.c_uint32),
        ("allow_gpu_copy", ct.c_uint32),
        ("allow_cpu_copy", ct.c_uint32),
    ]


class FrameProcessConfig(ct.Structure):
    _fields_ = [
        ("struct_size", ct.c_uint32),
        ("struct_version", ct.c_uint32),
        ("backend", ct.c_uint32),
        ("crop_x", ct.c_uint32),
        ("crop_y", ct.c_uint32),
        ("crop_width", ct.c_uint32),
        ("crop_height", ct.c_uint32),
        ("output_width", ct.c_uint32),
        ("output_height", ct.c_uint32),
        ("fit", ct.c_uint32),
        ("rotation", ct.c_uint32),
        ("flip_horizontal", ct.c_uint8),
        ("flip_vertical", ct.c_uint8),
        ("reserved", ct.c_uint8 * 2),
        ("background_rgba", ct.c_uint32),
    ]


class GpuFrameDesc(ct.Structure):
    _fields_ = [
        ("struct_size", ct.c_uint32), ("struct_version", ct.c_uint32),
        ("backend", ct.c_uint32), ("memory_type", ct.c_uint32),
        ("device_id", ct.c_uint64), ("generation", ct.c_uint64),
        ("pixel_format", ct.c_uint32), ("width", ct.c_uint32),
        ("height", ct.c_uint32), ("plane_count", ct.c_uint32),
        ("plane_offsets", ct.c_uint64 * 4), ("pitches", ct.c_uint64 * 4),
        ("pts", ct.c_int64), ("color_primaries", ct.c_uint32),
        ("color_transfer", ct.c_uint32), ("color_matrix", ct.c_uint32),
        ("color_range", ct.c_uint32),
    ]


class GpuNativeHandleDesc(ct.Structure):
    _fields_ = [
        ("struct_size", ct.c_uint32), ("struct_version", ct.c_uint32),
        ("type", ct.c_uint32), ("borrowed", ct.c_uint32),
        ("device_id", ct.c_uint64), ("generation", ct.c_uint64),
        ("handles", ct.c_uint64 * 4),
    ]


class GpuExternalFrameConfig(ct.Structure):
    _fields_ = [
        ("struct_size", ct.c_uint32), ("struct_version", ct.c_uint32),
        ("frame", GpuFrameDesc), ("native_handle", GpuNativeHandleDesc),
        ("query", ct.c_void_p), ("release", ct.c_void_p),
        ("user_data", ct.c_void_p),
    ]


def _candidate_paths() -> list[str]:
    explicit = os.environ.get("MKVC_LIBRARY_PATH")
    candidates = [explicit] if explicit else []
    package_dir = Path(__file__).resolve().parent
    names = ("mkvcodec.dll", "libmkvcodec.so", "libmkvcodec.dylib")
    candidates.extend(str(package_dir / name) for name in names)
    discovered = ctypes.util.find_library("mkvcodec")
    if discovered:
        candidates.append(discovered)
    return [candidate for candidate in candidates if candidate]


def _load() -> ct.CDLL:
    errors: list[str] = []
    for candidate in _candidate_paths():
        try:
            return ct.CDLL(candidate)
        except OSError as exc:
            errors.append(f"{candidate}: {exc}")
    detail = "; ".join(errors) or "no candidate library was found"
    raise ImportError(
        "Unable to load mkvcodec native library. Set MKVC_LIBRARY_PATH. " + detail
    )


lib = _load()
EncoderHandle = ct.c_void_p
DecoderHandle = ct.c_void_p
FrameHandle = ct.c_void_p
GpuFrameHandle = ct.c_void_p
SubmissionHandle = ct.c_void_p
CpuFramePoolHandle = ct.c_void_p
CpuBufferHandle = ct.c_void_p
GpuResourcePoolHandle = ct.c_void_p
GpuResourceReservationHandle = ct.c_void_p

lib.mkvc_get_backend_capabilities.argtypes = [
    ct.POINTER(BackendCapability), ct.POINTER(ct.c_size_t)
]
lib.mkvc_get_backend_capabilities.restype = ct.c_int

lib.mkvc_encoder_create.argtypes = [ct.POINTER(EncoderConfig), ct.POINTER(EncoderHandle)]
lib.mkvc_encoder_create.restype = ct.c_int
lib.mkvc_encoder_set_copy_policy.argtypes = [
    EncoderHandle, ct.POINTER(CopyPolicy)
]
lib.mkvc_encoder_set_copy_policy.restype = ct.c_int
lib.mkvc_encoder_write_frame.argtypes = [EncoderHandle, ct.POINTER(FrameView)]
lib.mkvc_encoder_write_frame.restype = ct.c_int
lib.mkvc_encoder_write_frame_borrowed.argtypes = [
    EncoderHandle, ct.POINTER(FrameView)
]
lib.mkvc_encoder_write_frame_borrowed.restype = ct.c_int
lib.mkvc_encoder_submit_frame_borrowed.argtypes = [
    EncoderHandle, ct.POINTER(FrameView), ct.POINTER(SubmissionHandle)
]
lib.mkvc_encoder_submit_frame_borrowed.restype = ct.c_int
lib.mkvc_encoder_write_gpu_frame.argtypes = [EncoderHandle, GpuFrameHandle]
lib.mkvc_encoder_write_gpu_frame.restype = ct.c_int
lib.mkvc_encoder_try_write_frame.argtypes = [EncoderHandle, ct.POINTER(FrameView)]
lib.mkvc_encoder_try_write_frame.restype = ct.c_int
lib.mkvc_encoder_flush.argtypes = [EncoderHandle]
lib.mkvc_encoder_flush.restype = ct.c_int
lib.mkvc_encoder_cancel.argtypes = [EncoderHandle]
lib.mkvc_encoder_cancel.restype = ct.c_int
lib.mkvc_encoder_close.argtypes = [EncoderHandle]
lib.mkvc_encoder_close.restype = ct.c_int
lib.mkvc_encoder_get_metrics.argtypes = [EncoderHandle, ct.POINTER(PipelineMetrics)]
lib.mkvc_encoder_get_metrics.restype = ct.c_int
lib.mkvc_encoder_destroy.argtypes = [EncoderHandle]
lib.mkvc_submission_query.argtypes = [
    SubmissionHandle, ct.POINTER(ct.c_uint32)
]
lib.mkvc_submission_query.restype = ct.c_int
lib.mkvc_submission_wait.argtypes = [SubmissionHandle, ct.c_uint32]
lib.mkvc_submission_wait.restype = ct.c_int
lib.mkvc_submission_release.argtypes = [SubmissionHandle]

lib.mkvc_cpu_frame_pool_create.argtypes = [
    ct.POINTER(CpuFramePoolConfig), ct.POINTER(CpuFramePoolHandle)
]
lib.mkvc_cpu_frame_pool_create.restype = ct.c_int
lib.mkvc_cpu_frame_pool_destroy.argtypes = [CpuFramePoolHandle]
lib.mkvc_cpu_frame_pool_acquire.argtypes = [
    CpuFramePoolHandle, ct.c_uint32, ct.POINTER(CpuBufferHandle)
]
lib.mkvc_cpu_frame_pool_acquire.restype = ct.c_int
lib.mkvc_cpu_buffer_get_desc.argtypes = [
    CpuBufferHandle, ct.POINTER(CpuBufferDesc)
]
lib.mkvc_cpu_buffer_get_desc.restype = ct.c_int
lib.mkvc_cpu_buffer_get_view.argtypes = [
    CpuBufferHandle, ct.POINTER(MutableFrameView)
]
lib.mkvc_cpu_buffer_get_view.restype = ct.c_int
lib.mkvc_cpu_buffer_release.argtypes = [CpuBufferHandle]
lib.mkvc_encoder_submit_cpu_buffer.argtypes = [
    EncoderHandle, CpuBufferHandle, ct.c_int64, ct.POINTER(SubmissionHandle)
]
lib.mkvc_encoder_submit_cpu_buffer.restype = ct.c_int

lib.mkvc_gpu_resource_pool_create.argtypes = [
    ct.POINTER(GpuResourcePoolConfig), ct.POINTER(GpuResourcePoolHandle)]
lib.mkvc_gpu_resource_pool_create.restype = ct.c_int
lib.mkvc_gpu_resource_pool_destroy.argtypes = [GpuResourcePoolHandle]
lib.mkvc_gpu_resource_pool_acquire.argtypes = [
    GpuResourcePoolHandle, ct.c_uint32, ct.POINTER(GpuResourceReservationHandle)]
lib.mkvc_gpu_resource_pool_acquire.restype = ct.c_int
lib.mkvc_gpu_resource_reservation_get_desc.argtypes = [
    GpuResourceReservationHandle, ct.POINTER(GpuResourceReservationDesc)]
lib.mkvc_gpu_resource_reservation_get_desc.restype = ct.c_int
lib.mkvc_gpu_resource_reservation_release.argtypes = [GpuResourceReservationHandle]
lib.mkvc_gpu_resource_pool_get_stats.argtypes = [
    GpuResourcePoolHandle, ct.POINTER(GpuResourcePoolStats)]
lib.mkvc_gpu_resource_pool_get_stats.restype = ct.c_int

lib.mkvc_decoder_create.argtypes = [ct.POINTER(DecoderConfig), ct.POINTER(DecoderHandle)]
lib.mkvc_decoder_create.restype = ct.c_int
lib.mkvc_decoder_set_copy_policy.argtypes = [
    DecoderHandle, ct.POINTER(CopyPolicy)
]
lib.mkvc_decoder_set_copy_policy.restype = ct.c_int
lib.mkvc_decoder_read.argtypes = [DecoderHandle, ct.POINTER(FrameHandle)]
lib.mkvc_decoder_read.restype = ct.c_int
lib.mkvc_decoder_read_gpu.argtypes = [DecoderHandle, ct.POINTER(GpuFrameHandle)]
lib.mkvc_decoder_read_gpu.restype = ct.c_int
lib.mkvc_decoder_close.argtypes = [DecoderHandle]
lib.mkvc_decoder_close.restype = ct.c_int
lib.mkvc_decoder_get_metrics.argtypes = [DecoderHandle, ct.POINTER(PipelineMetrics)]
lib.mkvc_decoder_get_metrics.restype = ct.c_int
lib.mkvc_decoder_destroy.argtypes = [DecoderHandle]

lib.mkvc_frame_get_view.argtypes = [FrameHandle, ct.POINTER(FrameView)]
lib.mkvc_frame_get_view.restype = ct.c_int
lib.mkvc_frame_copy_to.argtypes = [FrameHandle, ct.POINTER(MutableFrameView)]
lib.mkvc_frame_copy_to.restype = ct.c_int
lib.mkvc_frame_process.argtypes = [
    FrameHandle, ct.POINTER(FrameProcessConfig), ct.POINTER(FrameHandle)
]
lib.mkvc_frame_process.restype = ct.c_int
lib.mkvc_frame_release.argtypes = [FrameHandle]
lib.mkvc_gpu_frame_get_desc.argtypes = [GpuFrameHandle, ct.POINTER(GpuFrameDesc)]
lib.mkvc_gpu_frame_get_desc.restype = ct.c_int
lib.mkvc_gpu_frame_get_native_handle.argtypes = [
    GpuFrameHandle, ct.POINTER(GpuNativeHandleDesc)
]
lib.mkvc_gpu_frame_get_native_handle.restype = ct.c_int
lib.mkvc_gpu_frame_import_external.argtypes = [
    ct.POINTER(GpuExternalFrameConfig), ct.POINTER(GpuFrameHandle)
]
lib.mkvc_gpu_frame_import_external.restype = ct.c_int
lib.mkvc_gpu_frame_import_va_surface.argtypes = [
    ct.POINTER(GpuExternalFrameConfig), ct.POINTER(GpuFrameHandle)
]
lib.mkvc_gpu_frame_import_va_surface.restype = ct.c_int
lib.mkvc_gpu_frame_import_d3d11_fence.argtypes = [
    ct.POINTER(GpuExternalFrameConfig), ct.POINTER(GpuFrameHandle)
]
lib.mkvc_gpu_frame_import_d3d11_fence.restype = ct.c_int
lib.mkvc_gpu_frame_import_level_zero_event.argtypes = [
    ct.POINTER(GpuExternalFrameConfig), ct.POINTER(GpuFrameHandle)]
lib.mkvc_gpu_frame_import_level_zero_event.restype = ct.c_int
lib.mkvc_gpu_frame_import_cuda_event.argtypes = [
    ct.POINTER(GpuExternalFrameConfig), ct.POINTER(GpuFrameHandle)
]
lib.mkvc_gpu_frame_import_cuda_event.restype = ct.c_int
lib.mkvc_gpu_frame_wait.argtypes = [GpuFrameHandle, ct.c_uint32]
lib.mkvc_gpu_frame_wait.restype = ct.c_int
lib.mkvc_gpu_frame_export_dlpack.argtypes = [
    GpuFrameHandle, ct.c_uint32, ct.c_uint64, ct.POINTER(ct.c_void_p)
]
lib.mkvc_gpu_frame_export_dlpack.restype = ct.c_int
GpuDependencyCallback = ct.CFUNCTYPE(
    ct.c_int, ct.c_void_p, ct.c_uint64, ct.c_uint64)
lib.mkvc_gpu_frame_export_dlpack_with_dependency.argtypes = [
    GpuFrameHandle, ct.c_uint32, ct.c_uint64, GpuDependencyCallback,
    ct.c_void_p, ct.POINTER(ct.c_void_p)]
lib.mkvc_gpu_frame_export_dlpack_with_dependency.restype = ct.c_int
lib.mkvc_dlpack_managed_tensor_release.argtypes = [ct.c_void_p]
lib.mkvc_gpu_frame_retain.argtypes = [GpuFrameHandle]
lib.mkvc_gpu_frame_retain.restype = ct.c_int
lib.mkvc_gpu_frame_release.argtypes = [GpuFrameHandle]
lib.mkvc_get_last_error.restype = ct.c_char_p


def check(result: int) -> None:
    if result == MKVC_OK:
        return
    detail = lib.mkvc_get_last_error()
    message = detail.decode("utf-8", errors="replace") if detail else f"result={result}"
    if result == MKVC_ERROR_INVALID_STATE:
        raise RuntimeError(message)
    if result == MKVC_ERROR_TIMEOUT:
        raise TimeoutError(message)
    if result == MKVC_ERROR_CANCELLED:
        raise RuntimeError(message)
    raise ValueError(message)
