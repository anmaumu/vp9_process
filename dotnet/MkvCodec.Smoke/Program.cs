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
if (Marshal.SizeOf<MkvCpuBufferDescriptor>() != 32)
    throw new InvalidOperationException("MkvCpuBufferDescriptor ABI layout mismatch");
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
string borrowedPath = Path.Combine(
    Path.GetTempPath(), $"mkvcodec-dotnet-borrowed-{Guid.NewGuid():N}.webm");
string pooledPath = Path.Combine(
    Path.GetTempPath(), $"mkvcodec-dotnet-pool-{Guid.NewGuid():N}.webm");
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

    using (var writer = new MkvVideoWriter(
        borrowedPath, width, height, queueSize: 0))
    {
        writer.WriteBorrowedI420(y, u, v, pts: 0);
        y[0] ^= 0xff; // safe immediately after the synchronous call returns
    }
    using var borrowedCapture = new MkvVideoCapture(borrowedPath, prefetch: 0);
    if (borrowedCapture.ReadI420() is not { } borrowedFrame ||
        borrowedFrame.Width != width || borrowedFrame.Height != height ||
        borrowedCapture.ReadI420() is not null)
        throw new InvalidOperationException(".NET borrowed round-trip failed");

    using (var pool = new MkvCpuFramePool(
        MkvPixelFormat.I420, width, height, capacity: 1))
    using (var writer = new MkvVideoWriter(
        pooledPath, width, height, queueSize: 1))
    {
        using MkvCpuBuffer buffer = pool.Acquire();
        ulong firstGeneration = buffer.Generation;
        buffer.GetPlane(0).Fill(96);
        buffer.GetPlane(1).Fill(128);
        buffer.GetPlane(2).Fill(128);
        if (pool.TryAcquire(out _))
            throw new InvalidOperationException("Native CPU pool exceeded capacity");
        using MkvSubmission submission = writer.Submit(buffer, pts: 0);
        buffer.Dispose();
        submission.Wait(5000);
        if (submission.Status != MkvSubmissionStatus.Complete)
            throw new InvalidOperationException("Native CPU submission did not complete");
        using MkvCpuBuffer recycled = pool.Acquire(5000);
        if (recycled.Generation <= firstGeneration)
            throw new InvalidOperationException("Native CPU pool generation did not advance");
    }
    using var pooledCapture = new MkvVideoCapture(pooledPath, prefetch: 0);
    if (pooledCapture.ReadI420() is not { } pooledFrame ||
        pooledFrame.Width != width || pooledFrame.Height != height ||
        pooledCapture.ReadI420() is not null)
        throw new InvalidOperationException(".NET native-pool round-trip failed");

    int externalReleases = 0;
    var externalDescriptor = new MkvGpuFrameDescriptor {
        Backend = MkvBackend.Nvidia,
        MemoryType = MkvGpuMemoryType.CudaPointer,
        DeviceId = 1, Generation = 1,
        PixelFormat = MkvPixelFormat.Nv12,
        Width = width, Height = height, PlaneCount = 2,
        PlaneOffsets = [0, width * height, 0, 0],
        Pitches = [width, width, 0, 0]
    };
    var externalHandle = new MkvGpuNativeHandleDescriptor {
        Type = MkvGpuNativeHandleType.CudaPointer, Borrowed = 1,
        DeviceId = 1, Generation = 1,
        Handles = [0x1000, 0x2000, 0, 0]
    };
    bool externalReady = false;
    using (MkvGpuFrame imported = MkvGpuFrame.ImportExternal(
        externalDescriptor, externalHandle, new object(),
        producerReady: () => externalReady,
        release: _ => ++externalReleases))
    {
        if (imported.Descriptor.Width != width)
            throw new InvalidOperationException(".NET external GPU import failed");
        try
        {
            imported.Wait(0);
            throw new InvalidOperationException("Pending external GPU frame did not time out");
        }
        catch (MkvCodecException error) when (error.Result == MkvResult.Timeout) { }
        externalReady = true;
        imported.Wait(100);
    }
    if (externalReleases != 1)
        throw new InvalidOperationException(".NET external owner was not released once");
}
finally
{
    if (File.Exists(path)) File.Delete(path);
    if (File.Exists(borrowedPath)) File.Delete(borrowedPath);
    if (File.Exists(pooledPath)) File.Delete(pooledPath);
}

[StructLayout(LayoutKind.Sequential)]
struct NativeCopyPolicyForSmoke
{
    public uint StructSize, StructVersion, RequireGpuResident,
        AllowGpuCopy, AllowCpuCopy;
}
