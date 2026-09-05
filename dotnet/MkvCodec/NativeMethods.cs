using System.Reflection;
using System.Runtime.InteropServices;

namespace MkvCodec;

internal static class NativeMethods
{
    internal const string LibraryName = "mkvcodec";

    static NativeMethods()
    {
        NativeLibrary.SetDllImportResolver(typeof(NativeMethods).Assembly, Resolve);
    }

    private static nint Resolve(string libraryName, Assembly assembly,
                                DllImportSearchPath? searchPath)
    {
        if (libraryName != LibraryName) return nint.Zero;
        string? explicitPath = Environment.GetEnvironmentVariable("MKVC_LIBRARY_PATH");
        if (!string.IsNullOrWhiteSpace(explicitPath) &&
            NativeLibrary.TryLoad(explicitPath, out nint handle)) return handle;
        return nint.Zero;
    }

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern MkvResult mkvc_get_version(ref MkvVersion version);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern MkvResult mkvc_get_backend_capabilities(
        nint capabilities, ref nuint count);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nint mkvc_get_last_error();

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern MkvResult mkvc_encoder_create(
        ref NativeEncoderConfig config, out MkvEncoderHandle encoder);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern MkvResult mkvc_encoder_set_copy_policy(
        MkvEncoderHandle encoder, ref NativeCopyPolicy policy);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern MkvResult mkvc_encoder_write_frame(
        MkvEncoderHandle encoder, ref NativeFrameView frame);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern MkvResult mkvc_encoder_write_frame_borrowed(
        MkvEncoderHandle encoder, ref NativeFrameView frame);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern MkvResult mkvc_encoder_submit_cpu_buffer(
        MkvEncoderHandle encoder, MkvCpuBufferHandle buffer, long pts,
        out MkvSubmissionHandle submission);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern MkvResult mkvc_encoder_write_gpu_frame(
        MkvEncoderHandle encoder, MkvGpuFrameHandle frame);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern MkvResult mkvc_encoder_flush(MkvEncoderHandle encoder);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern MkvResult mkvc_encoder_cancel(MkvEncoderHandle encoder);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern MkvResult mkvc_encoder_close(nint encoder);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern MkvResult mkvc_encoder_get_metrics(
        nint encoder, ref MkvPipelineMetrics metrics);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void mkvc_encoder_destroy(nint encoder);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern MkvResult mkvc_submission_query(
        MkvSubmissionHandle submission, out MkvSubmissionStatus status);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern MkvResult mkvc_submission_wait(
        MkvSubmissionHandle submission, uint timeoutMilliseconds);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void mkvc_submission_release(nint submission);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern MkvResult mkvc_cpu_frame_pool_create(
        ref NativeCpuFramePoolConfig config, out MkvCpuFramePoolHandle pool);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void mkvc_cpu_frame_pool_destroy(nint pool);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern MkvResult mkvc_cpu_frame_pool_acquire(
        MkvCpuFramePoolHandle pool, uint timeoutMilliseconds,
        out MkvCpuBufferHandle buffer);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern MkvResult mkvc_cpu_buffer_get_desc(
        MkvCpuBufferHandle buffer, ref MkvCpuBufferDescriptor descriptor);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern MkvResult mkvc_cpu_buffer_get_view(
        MkvCpuBufferHandle buffer, ref NativeMutableFrameView view);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void mkvc_cpu_buffer_release(nint buffer);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern MkvResult mkvc_gpu_resource_pool_create(
        ref NativeGpuResourcePoolConfig config, out MkvGpuResourcePoolHandle pool);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void mkvc_gpu_resource_pool_destroy(nint pool);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern MkvResult mkvc_gpu_resource_pool_acquire(
        MkvGpuResourcePoolHandle pool, uint timeoutMilliseconds,
        out MkvGpuResourceReservationHandle reservation);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern MkvResult mkvc_gpu_resource_reservation_get_desc(
        MkvGpuResourceReservationHandle reservation,
        ref MkvGpuResourceReservationDescriptor descriptor);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void mkvc_gpu_resource_reservation_release(nint reservation);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern MkvResult mkvc_gpu_resource_pool_get_stats(
        MkvGpuResourcePoolHandle pool, ref MkvGpuResourcePoolStatistics statistics);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern MkvResult mkvc_decoder_create(
        ref NativeDecoderConfig config, out MkvDecoderHandle decoder);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern MkvResult mkvc_decoder_set_copy_policy(
        MkvDecoderHandle decoder, ref NativeCopyPolicy policy);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern MkvResult mkvc_decoder_read(
        MkvDecoderHandle decoder, out MkvFrameHandle frame);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern MkvResult mkvc_decoder_read_gpu(
        MkvDecoderHandle decoder, out MkvGpuFrameHandle frame);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern MkvResult mkvc_decoder_close(nint decoder);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern MkvResult mkvc_decoder_get_metrics(
        nint decoder, ref MkvPipelineMetrics metrics);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void mkvc_decoder_destroy(nint decoder);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void mkvc_frame_release(nint frame);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern MkvResult mkvc_frame_get_view(
        MkvFrameHandle frame, ref NativeFrameView view);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void mkvc_gpu_frame_release(nint frame);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern MkvResult mkvc_gpu_frame_get_desc(
        MkvGpuFrameHandle frame, ref MkvGpuFrameDescriptor descriptor);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern MkvResult mkvc_gpu_frame_get_native_handle(
        MkvGpuFrameHandle frame, ref MkvGpuNativeHandleDescriptor descriptor);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern MkvResult mkvc_gpu_frame_wait(
        MkvGpuFrameHandle frame, uint timeoutMilliseconds);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern MkvResult mkvc_gpu_frame_import_external(
        ref NativeGpuExternalFrameConfig config, out MkvGpuFrameHandle frame);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern MkvResult mkvc_gpu_frame_import_va_surface(
        ref NativeGpuExternalFrameConfig config, out MkvGpuFrameHandle frame);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern MkvResult mkvc_gpu_frame_import_d3d11_fence(
        ref NativeGpuExternalFrameConfig config, out MkvGpuFrameHandle frame);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern MkvResult mkvc_gpu_frame_import_cuda_event(
        ref NativeGpuExternalFrameConfig config, out MkvGpuFrameHandle frame);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern MkvResult mkvc_gpu_frame_import_level_zero_event(
        ref NativeGpuExternalFrameConfig config, out MkvGpuFrameHandle frame);
}
