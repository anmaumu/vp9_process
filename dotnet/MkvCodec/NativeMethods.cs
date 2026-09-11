using System.Reflection;
using System.Runtime.InteropServices;

namespace MkvCodec;

internal static partial class NativeMethods
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

    [DllImport(LibraryName, EntryPoint = "mkvc_submission_query",
        CallingConvention = CallingConvention.Cdecl)]
    internal static extern MkvResult mkvc_submission_query_raw(
        nint submission, out MkvSubmissionStatus status);

    [DllImport(LibraryName, EntryPoint = "mkvc_submission_wait",
        CallingConvention = CallingConvention.Cdecl)]
    internal static extern MkvResult mkvc_submission_wait_raw(
        nint submission, uint timeoutMilliseconds);

    // BEGIN MKVC GENERATED PINVOKE DECLARATIONS
    // END MKVC GENERATED PINVOKE DECLARATIONS
}
