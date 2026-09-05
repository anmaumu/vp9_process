"""Generated ctypes signatures; run tools/generate_bindings.py."""

from __future__ import annotations

import ctypes as ct
from typing import Any


def configure(lib: ct.CDLL, t: dict[str, Any]) -> None:
    """Apply generated argument and result types to the loaded ABI.

    Parameters
    ----------
    lib : ctypes.CDLL
        Loaded mkvcodec shared library.
    t : dict of str to Any
        Native module namespace containing generated type dependencies.
    """
    lib.mkvc_cpu_buffer_get_desc.argtypes = [t["CpuBufferHandle"], ct.POINTER(t["CpuBufferDesc"])]
    lib.mkvc_cpu_buffer_get_desc.restype = ct.c_int
    lib.mkvc_cpu_buffer_get_view.argtypes = [
        t["CpuBufferHandle"],
        ct.POINTER(t["MutableFrameView"]),
    ]
    lib.mkvc_cpu_buffer_get_view.restype = ct.c_int
    lib.mkvc_cpu_buffer_release.argtypes = [t["CpuBufferHandle"]]
    lib.mkvc_cpu_buffer_release.restype = None
    lib.mkvc_cpu_frame_pool_acquire.argtypes = [
        t["CpuFramePoolHandle"],
        ct.c_uint32,
        ct.POINTER(t["CpuBufferHandle"]),
    ]
    lib.mkvc_cpu_frame_pool_acquire.restype = ct.c_int
    lib.mkvc_cpu_frame_pool_create.argtypes = [
        ct.POINTER(t["CpuFramePoolConfig"]),
        ct.POINTER(t["CpuFramePoolHandle"]),
    ]
    lib.mkvc_cpu_frame_pool_create.restype = ct.c_int
    lib.mkvc_cpu_frame_pool_destroy.argtypes = [t["CpuFramePoolHandle"]]
    lib.mkvc_cpu_frame_pool_destroy.restype = None
    lib.mkvc_decoder_close.argtypes = [t["DecoderHandle"]]
    lib.mkvc_decoder_close.restype = ct.c_int
    lib.mkvc_decoder_create.argtypes = [
        ct.POINTER(t["DecoderConfig"]),
        ct.POINTER(t["DecoderHandle"]),
    ]
    lib.mkvc_decoder_create.restype = ct.c_int
    lib.mkvc_decoder_destroy.argtypes = [t["DecoderHandle"]]
    lib.mkvc_decoder_destroy.restype = None
    lib.mkvc_decoder_get_metrics.argtypes = [t["DecoderHandle"], ct.POINTER(t["PipelineMetrics"])]
    lib.mkvc_decoder_get_metrics.restype = ct.c_int
    lib.mkvc_decoder_read.argtypes = [t["DecoderHandle"], ct.POINTER(t["FrameHandle"])]
    lib.mkvc_decoder_read.restype = ct.c_int
    lib.mkvc_decoder_read_gpu.argtypes = [t["DecoderHandle"], ct.POINTER(t["GpuFrameHandle"])]
    lib.mkvc_decoder_read_gpu.restype = ct.c_int
    lib.mkvc_decoder_set_copy_policy.argtypes = [t["DecoderHandle"], ct.POINTER(t["CopyPolicy"])]
    lib.mkvc_decoder_set_copy_policy.restype = ct.c_int
    lib.mkvc_dlpack_managed_tensor_release.argtypes = [ct.c_void_p]
    lib.mkvc_dlpack_managed_tensor_release.restype = None
    lib.mkvc_encoder_cancel.argtypes = [t["EncoderHandle"]]
    lib.mkvc_encoder_cancel.restype = ct.c_int
    lib.mkvc_encoder_close.argtypes = [t["EncoderHandle"]]
    lib.mkvc_encoder_close.restype = ct.c_int
    lib.mkvc_encoder_create.argtypes = [
        ct.POINTER(t["EncoderConfig"]),
        ct.POINTER(t["EncoderHandle"]),
    ]
    lib.mkvc_encoder_create.restype = ct.c_int
    lib.mkvc_encoder_destroy.argtypes = [t["EncoderHandle"]]
    lib.mkvc_encoder_destroy.restype = None
    lib.mkvc_encoder_flush.argtypes = [t["EncoderHandle"]]
    lib.mkvc_encoder_flush.restype = ct.c_int
    lib.mkvc_encoder_get_metrics.argtypes = [t["EncoderHandle"], ct.POINTER(t["PipelineMetrics"])]
    lib.mkvc_encoder_get_metrics.restype = ct.c_int
    lib.mkvc_encoder_set_copy_policy.argtypes = [t["EncoderHandle"], ct.POINTER(t["CopyPolicy"])]
    lib.mkvc_encoder_set_copy_policy.restype = ct.c_int
    lib.mkvc_encoder_submit_cpu_buffer.argtypes = [
        t["EncoderHandle"],
        t["CpuBufferHandle"],
        ct.c_int64,
        ct.POINTER(t["SubmissionHandle"]),
    ]
    lib.mkvc_encoder_submit_cpu_buffer.restype = ct.c_int
    lib.mkvc_encoder_submit_frame_borrowed.argtypes = [
        t["EncoderHandle"],
        ct.POINTER(t["FrameView"]),
        ct.POINTER(t["SubmissionHandle"]),
    ]
    lib.mkvc_encoder_submit_frame_borrowed.restype = ct.c_int
    lib.mkvc_encoder_try_write_frame.argtypes = [t["EncoderHandle"], ct.POINTER(t["FrameView"])]
    lib.mkvc_encoder_try_write_frame.restype = ct.c_int
    lib.mkvc_encoder_write_frame.argtypes = [t["EncoderHandle"], ct.POINTER(t["FrameView"])]
    lib.mkvc_encoder_write_frame.restype = ct.c_int
    lib.mkvc_encoder_write_frame_borrowed.argtypes = [
        t["EncoderHandle"],
        ct.POINTER(t["FrameView"]),
    ]
    lib.mkvc_encoder_write_frame_borrowed.restype = ct.c_int
    lib.mkvc_encoder_write_gpu_frame.argtypes = [t["EncoderHandle"], t["GpuFrameHandle"]]
    lib.mkvc_encoder_write_gpu_frame.restype = ct.c_int
    lib.mkvc_frame_copy_to.argtypes = [t["FrameHandle"], ct.POINTER(t["MutableFrameView"])]
    lib.mkvc_frame_copy_to.restype = ct.c_int
    lib.mkvc_frame_get_view.argtypes = [t["FrameHandle"], ct.POINTER(t["FrameView"])]
    lib.mkvc_frame_get_view.restype = ct.c_int
    lib.mkvc_frame_process.argtypes = [
        t["FrameHandle"],
        ct.POINTER(t["FrameProcessConfig"]),
        ct.POINTER(t["FrameHandle"]),
    ]
    lib.mkvc_frame_process.restype = ct.c_int
    lib.mkvc_frame_release.argtypes = [t["FrameHandle"]]
    lib.mkvc_frame_release.restype = None
    lib.mkvc_frame_retain.argtypes = [t["FrameHandle"]]
    lib.mkvc_frame_retain.restype = None
    lib.mkvc_get_backend_capabilities.argtypes = [
        ct.POINTER(t["BackendCapability"]),
        ct.POINTER(ct.c_size_t),
    ]
    lib.mkvc_get_backend_capabilities.restype = ct.c_int
    lib.mkvc_get_last_error.argtypes = []
    lib.mkvc_get_last_error.restype = ct.c_char_p
    lib.mkvc_get_version.argtypes = [ct.POINTER(t["Version"])]
    lib.mkvc_get_version.restype = ct.c_int
    lib.mkvc_gpu_frame_export_dlpack.argtypes = [
        t["GpuFrameHandle"],
        ct.c_uint32,
        ct.c_uint64,
        ct.POINTER(ct.c_void_p),
    ]
    lib.mkvc_gpu_frame_export_dlpack.restype = ct.c_int
    lib.mkvc_gpu_frame_export_dlpack_with_dependency.argtypes = [
        t["GpuFrameHandle"],
        ct.c_uint32,
        ct.c_uint64,
        t["GpuDependencyCallback"],
        ct.c_void_p,
        ct.POINTER(ct.c_void_p),
    ]
    lib.mkvc_gpu_frame_export_dlpack_with_dependency.restype = ct.c_int
    lib.mkvc_gpu_frame_get_desc.argtypes = [t["GpuFrameHandle"], ct.POINTER(t["GpuFrameDesc"])]
    lib.mkvc_gpu_frame_get_desc.restype = ct.c_int
    lib.mkvc_gpu_frame_get_native_handle.argtypes = [
        t["GpuFrameHandle"],
        ct.POINTER(t["GpuNativeHandleDesc"]),
    ]
    lib.mkvc_gpu_frame_get_native_handle.restype = ct.c_int
    lib.mkvc_gpu_frame_import_cuda_event.argtypes = [
        ct.POINTER(t["GpuExternalFrameConfig"]),
        ct.POINTER(t["GpuFrameHandle"]),
    ]
    lib.mkvc_gpu_frame_import_cuda_event.restype = ct.c_int
    lib.mkvc_gpu_frame_import_d3d11_fence.argtypes = [
        ct.POINTER(t["GpuExternalFrameConfig"]),
        ct.POINTER(t["GpuFrameHandle"]),
    ]
    lib.mkvc_gpu_frame_import_d3d11_fence.restype = ct.c_int
    lib.mkvc_gpu_frame_import_external.argtypes = [
        ct.POINTER(t["GpuExternalFrameConfig"]),
        ct.POINTER(t["GpuFrameHandle"]),
    ]
    lib.mkvc_gpu_frame_import_external.restype = ct.c_int
    lib.mkvc_gpu_frame_import_level_zero_event.argtypes = [
        ct.POINTER(t["GpuExternalFrameConfig"]),
        ct.POINTER(t["GpuFrameHandle"]),
    ]
    lib.mkvc_gpu_frame_import_level_zero_event.restype = ct.c_int
    lib.mkvc_gpu_frame_import_va_surface.argtypes = [
        ct.POINTER(t["GpuExternalFrameConfig"]),
        ct.POINTER(t["GpuFrameHandle"]),
    ]
    lib.mkvc_gpu_frame_import_va_surface.restype = ct.c_int
    lib.mkvc_gpu_frame_query_completion.argtypes = [t["GpuFrameHandle"], ct.POINTER(ct.c_uint32)]
    lib.mkvc_gpu_frame_query_completion.restype = ct.c_int
    lib.mkvc_gpu_frame_release.argtypes = [t["GpuFrameHandle"]]
    lib.mkvc_gpu_frame_release.restype = None
    lib.mkvc_gpu_frame_retain.argtypes = [t["GpuFrameHandle"]]
    lib.mkvc_gpu_frame_retain.restype = ct.c_int
    lib.mkvc_gpu_frame_wait.argtypes = [t["GpuFrameHandle"], ct.c_uint32]
    lib.mkvc_gpu_frame_wait.restype = ct.c_int
    lib.mkvc_gpu_resource_pool_acquire.argtypes = [
        t["GpuResourcePoolHandle"],
        ct.c_uint32,
        ct.POINTER(t["GpuResourceReservationHandle"]),
    ]
    lib.mkvc_gpu_resource_pool_acquire.restype = ct.c_int
    lib.mkvc_gpu_resource_pool_create.argtypes = [
        ct.POINTER(t["GpuResourcePoolConfig"]),
        ct.POINTER(t["GpuResourcePoolHandle"]),
    ]
    lib.mkvc_gpu_resource_pool_create.restype = ct.c_int
    lib.mkvc_gpu_resource_pool_destroy.argtypes = [t["GpuResourcePoolHandle"]]
    lib.mkvc_gpu_resource_pool_destroy.restype = None
    lib.mkvc_gpu_resource_pool_get_stats.argtypes = [
        t["GpuResourcePoolHandle"],
        ct.POINTER(t["GpuResourcePoolStats"]),
    ]
    lib.mkvc_gpu_resource_pool_get_stats.restype = ct.c_int
    lib.mkvc_gpu_resource_reservation_get_desc.argtypes = [
        t["GpuResourceReservationHandle"],
        ct.POINTER(t["GpuResourceReservationDesc"]),
    ]
    lib.mkvc_gpu_resource_reservation_get_desc.restype = ct.c_int
    lib.mkvc_gpu_resource_reservation_release.argtypes = [t["GpuResourceReservationHandle"]]
    lib.mkvc_gpu_resource_reservation_release.restype = None
    lib.mkvc_result_string.argtypes = [ct.c_int]
    lib.mkvc_result_string.restype = ct.c_char_p
    lib.mkvc_submission_query.argtypes = [t["SubmissionHandle"], ct.POINTER(ct.c_uint32)]
    lib.mkvc_submission_query.restype = ct.c_int
    lib.mkvc_submission_release.argtypes = [t["SubmissionHandle"]]
    lib.mkvc_submission_release.restype = None
    lib.mkvc_submission_wait.argtypes = [t["SubmissionHandle"], ct.c_uint32]
    lib.mkvc_submission_wait.restype = ct.c_int
