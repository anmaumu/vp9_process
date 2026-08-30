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
public enum MkvPixelFormat : uint { I420 = 1, Nv12 = 2, Bgr24 = 3, Rgb24 = 4, Bgra32 = 5, P010 = 6 }
public enum MkvGpuMemoryType : uint { D3D11Texture = 1, VaSurface = 2, CudaPointer = 3, CudaArray = 4, Usm = 5 }
public enum MkvGpuNativeHandleType : uint { D3D11Texture = 1, VaSurface = 2, CudaPointer = 3, CudaArray = 4, UsmPointer = 5 }

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

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeFrameView
{
    internal uint StructSize;
    internal uint StructVersion;
    internal uint PixelFormat;
    internal uint Width;
    internal uint Height;
    internal nint Plane0;
    internal nint Plane1;
    internal nint Plane2;
    internal nint Plane3;
    internal int Stride0;
    internal int Stride1;
    internal int Stride2;
    internal int Stride3;
    internal long Pts;
}

[StructLayout(LayoutKind.Sequential)]
public struct MkvGpuFrameDescriptor
{
    public uint StructSize;
    public uint StructVersion;
    public MkvBackend Backend;
    public MkvGpuMemoryType MemoryType;
    public ulong DeviceId;
    public ulong Generation;
    public MkvPixelFormat PixelFormat;
    public uint Width;
    public uint Height;
    public uint PlaneCount;
    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 4)] public ulong[] PlaneOffsets;
    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 4)] public ulong[] Pitches;
    public long PtsNanoseconds;
    public uint ColorPrimaries;
    public uint ColorTransfer;
    public uint ColorMatrix;
    public uint ColorRange;
}

[StructLayout(LayoutKind.Sequential)]
public struct MkvGpuNativeHandleDescriptor
{
    public uint StructSize;
    public uint StructVersion;
    public MkvGpuNativeHandleType Type;
    public uint Borrowed;
    public ulong DeviceId;
    public ulong Generation;
    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 4)] public ulong[] Handles;
}
