using System.Runtime.InteropServices;

namespace MkvCodec;

public sealed class MkvCodecException : Exception
{
    public MkvResult Result { get; }

    internal MkvCodecException(MkvResult result)
        : base(ReadDetail(result)) => Result = result;

    private static string ReadDetail(MkvResult result)
    {
        nint pointer = NativeMethods.mkvc_get_last_error();
        string? detail = pointer == nint.Zero
            ? null : Marshal.PtrToStringUTF8(pointer);
        return string.IsNullOrEmpty(detail) ? $"mkvcodec failed: {result}" : detail;
    }
}
