using System.Runtime.InteropServices;

namespace MkvCodec;

public enum MkvResult : uint
{
    Ok = 0,
    InvalidArgument = 1,
    BufferTooSmall = 2,
    NotSupported = 3,
    Internal = 4,
    InvalidState = 5,
    Io = 6,
    Codec = 7,
    EndOfStream = 8,
    WouldBlock = 9,
}

public enum MkvBackend : uint { Cpu = 1, Nvidia = 2, Intel = 3 }
public enum MkvCodecKind : uint { Vp9 = 1, Av1 = 2 }

[StructLayout(LayoutKind.Sequential)]
public struct MkvVersion
{
    public uint StructSize;
    public uint AbiVersion;
    public uint Major;
    public uint Minor;
    public uint Patch;
}

[StructLayout(LayoutKind.Sequential)]
public struct MkvBackendCapability
{
    public uint StructSize;
    public MkvBackend Backend;
    public MkvCodecKind Codec;
    public byte CanDecode;
    public byte CanEncode;
    public byte IsHardware;
    public byte Reserved;
}

[StructLayout(LayoutKind.Sequential)]
public struct MkvPipelineMetrics
{
    public uint StructSize;
    public uint StructVersion;
    public ulong AcceptedFrames;
    public ulong CompletedFrames;
    public ulong RejectedFrames;
    public ulong QueueWaitNanoseconds;
    public ulong BackendTimeNanoseconds;
    public uint QueueCapacity;
    public uint PeakQueueDepth;
    public uint HardwarePendingPeak;
    public uint CopyPath;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeEncoderConfig
{
    internal uint StructSize;
    internal uint StructVersion;
    internal nint OutputPathUtf8;
    internal uint Codec;
    internal uint Backend;
    internal uint Width;
    internal uint Height;
    internal uint FpsNum;
    internal uint FpsDen;
    internal uint Quality;
    internal uint KeyframeIntervalFrames;
    internal uint Threads;
    internal uint QueueSize;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeDecoderConfig
{
    internal uint StructSize;
    internal uint StructVersion;
    internal nint InputPathUtf8;
    internal uint Codec;
    internal uint Backend;
    internal uint Threads;
    internal uint Prefetch;
}
