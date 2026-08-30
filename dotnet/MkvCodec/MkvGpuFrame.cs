using System.Runtime.InteropServices;

namespace MkvCodec;

/// <summary>Safe lease over an Intel or NVIDIA backend-owned GPU surface.</summary>
public sealed class MkvGpuFrame : IDisposable
{
    private MkvGpuFrameHandle? handle;

    internal MkvGpuFrame(MkvGpuFrameHandle handle) => this.handle = handle;

    internal MkvGpuFrameHandle BorrowHandle()
    {
        ObjectDisposedException.ThrowIf(handle is null || handle.IsClosed, this);
        return handle!;
    }

    public MkvGpuFrameDescriptor Descriptor
    {
        get
        {
            ObjectDisposedException.ThrowIf(handle is null || handle.IsClosed, this);
            var value = new MkvGpuFrameDescriptor {
                StructSize = checked((uint)Marshal.SizeOf<MkvGpuFrameDescriptor>()),
                StructVersion = 1,
                PlaneOffsets = new ulong[4], Pitches = new ulong[4]
            };
            MkvCodecInfo.ThrowIfFailed(
                NativeMethods.mkvc_gpu_frame_get_desc(handle!, ref value));
            return value;
        }
    }

    public MkvGpuNativeHandleDescriptor NativeHandle
    {
        get
        {
            ObjectDisposedException.ThrowIf(handle is null || handle.IsClosed, this);
            var value = new MkvGpuNativeHandleDescriptor {
                StructSize = checked((uint)Marshal.SizeOf<MkvGpuNativeHandleDescriptor>()),
                StructVersion = 1, Handles = new ulong[4]
            };
            MkvCodecInfo.ThrowIfFailed(
                NativeMethods.mkvc_gpu_frame_get_native_handle(handle!, ref value));
            return value;
        }
    }

    public void Wait(uint timeoutMilliseconds = uint.MaxValue)
    {
        ObjectDisposedException.ThrowIf(handle is null || handle.IsClosed, this);
        MkvCodecInfo.ThrowIfFailed(
            NativeMethods.mkvc_gpu_frame_wait(handle!, timeoutMilliseconds));
    }

    public void Dispose()
    {
        handle?.Dispose();
        handle = null;
        GC.SuppressFinalize(this);
    }
}
