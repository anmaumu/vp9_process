using System.Runtime.InteropServices;

namespace MkvCodec;

public static class MkvCodecInfo
{
    public static MkvVersion Version
    {
        get
        {
            var version = new MkvVersion {
                StructSize = checked((uint)Marshal.SizeOf<MkvVersion>())
            };
            ThrowIfFailed(NativeMethods.mkvc_get_version(ref version));
            return version;
        }
    }

    public static unsafe IReadOnlyList<MkvBackendCapability> GetCapabilities()
    {
        nuint count = 0;
        ThrowIfFailed(NativeMethods.mkvc_get_backend_capabilities(nint.Zero, ref count));
        if (count == 0) return Array.Empty<MkvBackendCapability>();
        var capabilities = new MkvBackendCapability[checked((int)count)];
        fixed (MkvBackendCapability* pointer = capabilities)
        {
            ThrowIfFailed(NativeMethods.mkvc_get_backend_capabilities(
                (nint)pointer, ref count));
        }
        if (count != (nuint)capabilities.Length) Array.Resize(ref capabilities, (int)count);
        return capabilities;
    }

    internal static void ThrowIfFailed(MkvResult result)
    {
        if (result != MkvResult.Ok) throw new MkvCodecException(result);
    }
}
