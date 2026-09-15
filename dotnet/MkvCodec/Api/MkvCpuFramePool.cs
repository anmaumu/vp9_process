using System.Runtime.InteropServices;

namespace MkvCodec;

/// <summary>Fixed-capacity native CPU input pool that does not pin managed arrays.</summary>
public sealed class MkvCpuFramePool : IDisposable
{
    private MkvCpuFramePoolHandle? handle;
    private MkvCpuFramePoolStatistics? finalStatistics;

    public MkvCpuFramePool(MkvPixelFormat pixelFormat, uint width, uint height,
                           uint capacity, bool pageLocked = false)
    {
        if (capacity == 0) throw new ArgumentOutOfRangeException(nameof(capacity));
        var config = new NativeCpuFramePoolConfig {
            StructSize = checked((uint)Marshal.SizeOf<NativeCpuFramePoolConfig>()),
            StructVersion = 1, PixelFormat = pixelFormat,
            Width = width, Height = height, Capacity = capacity
        };
        var options = new NativeCpuFramePoolOptions {
            StructSize = checked((uint)Marshal.SizeOf<NativeCpuFramePoolOptions>()),
            StructVersion = 1,
            MemoryMode = pageLocked ? MkvCpuMemoryMode.PageLocked : MkvCpuMemoryMode.Pageable
        };
        MkvCodecInfo.ThrowIfFailed(
            NativeMethods.mkvc_cpu_frame_pool_create_ex(ref config, ref options, out handle));
        PixelFormat = pixelFormat;
        Width = width;
        Height = height;
        Capacity = capacity;
        PageLocked = pageLocked;
    }

    public MkvPixelFormat PixelFormat { get; }
    public uint Width { get; }
    public uint Height { get; }
    public uint Capacity { get; }
    public bool PageLocked { get; }

    /// <summary>Current or final allocation, occupancy, wait, and lease metrics.</summary>
    public MkvCpuFramePoolStatistics Statistics => finalStatistics ?? ReadStatistics();

    private MkvCpuFramePoolStatistics ReadStatistics()
    {
        ObjectDisposedException.ThrowIf(handle is null || handle.IsClosed, this);
        var value = new MkvCpuFramePoolStatistics {
            StructSize = checked((uint)Marshal.SizeOf<MkvCpuFramePoolStatistics>()),
            StructVersion = 1
        };
        MkvCodecInfo.ThrowIfFailed(
            NativeMethods.mkvc_cpu_frame_pool_get_stats(handle!, ref value));
        return value;
    }

    public MkvCpuBuffer Acquire(uint timeoutMilliseconds = uint.MaxValue)
    {
        ObjectDisposedException.ThrowIf(handle is null || handle.IsClosed, this);
        MkvCodecInfo.ThrowIfFailed(NativeMethods.mkvc_cpu_frame_pool_acquire(
            handle!, timeoutMilliseconds, out MkvCpuBufferHandle buffer));
        return new MkvCpuBuffer(buffer);
    }

    public bool TryAcquire(out MkvCpuBuffer? buffer)
    {
        ObjectDisposedException.ThrowIf(handle is null || handle.IsClosed, this);
        MkvResult result = NativeMethods.mkvc_cpu_frame_pool_acquire(
            handle!, 0, out MkvCpuBufferHandle nativeBuffer);
        if (result == MkvResult.WouldBlock)
        {
            buffer = null;
            return false;
        }
        MkvCodecInfo.ThrowIfFailed(result);
        buffer = new MkvCpuBuffer(nativeBuffer);
        return true;
    }

    public void Dispose()
    {
        if (handle is not null && !handle.IsClosed)
            finalStatistics = ReadStatistics();
        handle?.Dispose();
        handle = null;
        GC.SuppressFinalize(this);
    }
}
