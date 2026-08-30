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
    internal static extern MkvResult mkvc_encoder_write_gpu_frame(
        MkvEncoderHandle encoder, MkvGpuFrameHandle frame);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern MkvResult mkvc_encoder_flush(MkvEncoderHandle encoder);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern MkvResult mkvc_encoder_close(nint encoder);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern MkvResult mkvc_encoder_get_metrics(
        nint encoder, ref MkvPipelineMetrics metrics);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void mkvc_encoder_destroy(nint encoder);

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
}
