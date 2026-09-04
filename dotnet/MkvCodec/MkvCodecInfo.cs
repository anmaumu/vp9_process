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

    /// <summary>Selects one runtime backend satisfying every requested direction.</summary>
    public static MkvBackend SelectBackend(MkvCodecKind codec,
        bool decode = true, bool encode = true, bool requireGpuResident = false)
    {
        if (!decode && !encode)
            throw new ArgumentException("At least one pipeline direction is required");
        var candidates = GetCapabilities().Where(item =>
            item.Codec == codec && (!decode || item.CanDecode != 0) &&
            (!encode || item.CanEncode != 0) &&
            (!requireGpuResident || item.IsHardware != 0)).Select(item => item.Backend).ToHashSet();
        MkvBackend[] preference = encode && codec == MkvCodecKind.Vp9
            ? [MkvBackend.Intel, MkvBackend.Cpu]
            : [MkvBackend.Nvidia, MkvBackend.Intel, MkvBackend.Cpu];
        foreach (var backend in preference)
            if (candidates.Contains(backend)) return backend;
        string residence = requireGpuResident ? " GPU-resident" : string.Empty;
        string directions = decode && encode ? "decode/encode" : decode ? "decode" : "encode";
        throw new NotSupportedException($"No{residence} {codec} {directions} backend is available");
    }

    internal static void ThrowIfFailed(MkvResult result)
    {
        if (result != MkvResult.Ok) throw new MkvCodecException(result);
    }
}
