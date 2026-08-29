using System.Runtime.InteropServices;
using MkvCodec;

if (Marshal.SizeOf<MkvVersion>() != 20)
    throw new InvalidOperationException("MkvVersion ABI layout mismatch");
if (Marshal.SizeOf<MkvBackendCapability>() != 16)
    throw new InvalidOperationException("MkvBackendCapability ABI layout mismatch");

MkvVersion version = MkvCodecInfo.Version;
if (version.AbiVersion != 1 || version.StructSize != 20)
    throw new InvalidOperationException("Unexpected native ABI version");

IReadOnlyList<MkvBackendCapability> capabilities = MkvCodecInfo.GetCapabilities();
foreach (MkvBackendCapability capability in capabilities)
{
    if (capability.StructSize != 16 ||
        (capability.CanDecode == 0 && capability.CanEncode == 0))
        throw new InvalidOperationException("Invalid native capability row");
}

Console.WriteLine($"mkvcodec ABI {version.AbiVersion}: {capabilities.Count} capabilities");
