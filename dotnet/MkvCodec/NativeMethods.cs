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
    internal static extern MkvResult mkvc_encoder_close(nint encoder);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void mkvc_encoder_destroy(nint encoder);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern MkvResult mkvc_decoder_close(nint decoder);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void mkvc_decoder_destroy(nint decoder);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void mkvc_frame_release(nint frame);
}
