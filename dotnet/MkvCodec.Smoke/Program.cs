using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using MkvCodec;

if (Marshal.SizeOf<MkvVersion>() != 20)
    throw new InvalidOperationException("MkvVersion ABI layout mismatch");
if (Marshal.SizeOf<MkvBackendCapability>() != 16)
    throw new InvalidOperationException("MkvBackendCapability ABI layout mismatch");
if (Marshal.SizeOf<MkvPipelineMetrics>() != 64)
    throw new InvalidOperationException("MkvPipelineMetrics ABI layout mismatch");
if (Marshal.SizeOf<MkvGpuFrameDescriptor>() != 136)
    throw new InvalidOperationException("MkvGpuFrameDescriptor ABI layout mismatch");
if (Marshal.SizeOf<MkvGpuNativeHandleDescriptor>() != 64)
    throw new InvalidOperationException("MkvGpuNativeHandleDescriptor ABI layout mismatch");
if (Marshal.SizeOf(typeof(NativeCopyPolicyForSmoke)) != 20)
    throw new InvalidOperationException("copy policy ABI layout mismatch");

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

string path = Path.Combine(Path.GetTempPath(), $"mkvcodec-dotnet-{Guid.NewGuid():N}.webm");
try
{
    const uint width = 64, height = 48;
    byte[] y = new byte[width * height];
    byte[] u = new byte[width * height / 4];
    byte[] v = new byte[width * height / 4];
    Array.Fill(u, (byte)96);
    Array.Fill(v, (byte)160);
    using (var writer = new MkvVideoWriter(path, width, height, queueSize: 2))
    {
        for (int frame = 0; frame < 10; ++frame)
        {
            Array.Fill(y, checked((byte)(64 + frame)));
            writer.WriteI420(y, u, v);
        }
        writer.Flush();
    }
    using var capture = new MkvVideoCapture(path, prefetch: 2);
    int count = 0;
    long previousPts = -1;
    while (capture.ReadI420() is { } frame)
    {
        if (frame.Width != width || frame.Height != height ||
            frame.PtsNanoseconds <= previousPts || frame.Y.Length != width * height)
            throw new InvalidOperationException("Invalid .NET decoded frame");
        previousPts = frame.PtsNanoseconds;
        ++count;
    }
    if (count != 10) throw new InvalidOperationException(".NET frame count mismatch");
}
finally { if (File.Exists(path)) File.Delete(path); }

[StructLayout(LayoutKind.Sequential)]
struct NativeCopyPolicyForSmoke
{
    public uint StructSize, StructVersion, RequireGpuResident,
        AllowGpuCopy, AllowCpuCopy;
}
