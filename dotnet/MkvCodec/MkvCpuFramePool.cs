using System.Runtime.InteropServices;

namespace MkvCodec;

/// <summary>Fixed-capacity native CPU input pool that does not pin managed arrays.</summary>
public sealed class MkvCpuFramePool : IDisposable
{
    private MkvCpuFramePoolHandle? handle;

    public MkvCpuFramePool(MkvPixelFormat pixelFormat, uint width, uint height,
                           uint capacity)
    {
        if (capacity == 0) throw new ArgumentOutOfRangeException(nameof(capacity));
        var config = new NativeCpuFramePoolConfig {
            StructSize = checked((uint)Marshal.SizeOf<NativeCpuFramePoolConfig>()),
            StructVersion = 1, PixelFormat = pixelFormat,
            Width = width, Height = height, Capacity = capacity
        };
        MkvCodecInfo.ThrowIfFailed(
            NativeMethods.mkvc_cpu_frame_pool_create(ref config, out handle));
        PixelFormat = pixelFormat;
        Width = width;
        Height = height;
        Capacity = capacity;
    }

    public MkvPixelFormat PixelFormat { get; }
    public uint Width { get; }
    public uint Height { get; }
    public uint Capacity { get; }

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
        handle?.Dispose();
        handle = null;
        GC.SuppressFinalize(this);
    }
}

/// <summary>Generation-checked writable lease over one native CPU pool slot.</summary>
public sealed class MkvCpuBuffer : IDisposable
{
    private MkvCpuBufferHandle? handle;
    private readonly NativeMutableFrameView view;

    internal MkvCpuBuffer(MkvCpuBufferHandle handle)
    {
        this.handle = handle;
        var descriptor = new MkvCpuBufferDescriptor {
            StructSize = checked((uint)Marshal.SizeOf<MkvCpuBufferDescriptor>()),
            StructVersion = 1
        };
        var mutableView = new NativeMutableFrameView {
            StructSize = checked((uint)Marshal.SizeOf<NativeMutableFrameView>()),
            StructVersion = 1
        };
        try
        {
            MkvCodecInfo.ThrowIfFailed(
                NativeMethods.mkvc_cpu_buffer_get_desc(handle, ref descriptor));
            MkvCodecInfo.ThrowIfFailed(
                NativeMethods.mkvc_cpu_buffer_get_view(handle, ref mutableView));
        }
        catch
        {
            handle.Dispose();
            this.handle = null;
            throw;
        }
        Descriptor = descriptor;
        view = mutableView;
    }

    public MkvCpuBufferDescriptor Descriptor { get; }
    public MkvPixelFormat PixelFormat => Descriptor.PixelFormat;
    public uint Width => Descriptor.Width;
    public uint Height => Descriptor.Height;
    public ulong Generation => Descriptor.Generation;

    /// <summary>
    /// Returns writable native memory. The span becomes invalid on Dispose and
    /// must not be accessed or modified while an encoder submission is pending.
    /// </summary>
    public unsafe Span<byte> GetPlane(int index)
    {
        ObjectDisposedException.ThrowIf(handle is null || handle.IsClosed, this);
        if ((uint)index >= Descriptor.PlaneCount)
            throw new ArgumentOutOfRangeException(nameof(index));
        nint pointer = index switch {
            0 => view.Plane0, 1 => view.Plane1, 2 => view.Plane2, 3 => view.Plane3,
            _ => nint.Zero
        };
        int stride = GetStride(index);
        uint rows = index == 0 ? Height : PixelFormat switch {
            MkvPixelFormat.I420 or MkvPixelFormat.Nv12 => Height / 2,
            _ => Height
        };
        return new Span<byte>((void*)pointer, checked(stride * (int)rows));
    }

    public int GetStride(int index)
    {
        ObjectDisposedException.ThrowIf(handle is null || handle.IsClosed, this);
        if ((uint)index >= Descriptor.PlaneCount)
            throw new ArgumentOutOfRangeException(nameof(index));
        return index switch {
            0 => view.Stride0, 1 => view.Stride1, 2 => view.Stride2,
            3 => view.Stride3, _ => throw new ArgumentOutOfRangeException(nameof(index))
        };
    }

    internal MkvCpuBufferHandle BorrowHandle()
    {
        ObjectDisposedException.ThrowIf(handle is null || handle.IsClosed, this);
        return handle!;
    }

    public void Dispose()
    {
        handle?.Dispose();
        handle = null;
        GC.SuppressFinalize(this);
    }
}

/// <summary>Completion lease for one asynchronously submitted native CPU buffer.</summary>
public sealed class MkvSubmission : IDisposable
{
    private MkvSubmissionHandle? handle;
    internal MkvSubmission(MkvSubmissionHandle handle) => this.handle = handle;

    public MkvSubmissionStatus Status
    {
        get
        {
            ObjectDisposedException.ThrowIf(handle is null || handle.IsClosed, this);
            MkvCodecInfo.ThrowIfFailed(
                NativeMethods.mkvc_submission_query(handle!, out MkvSubmissionStatus status));
            return status;
        }
    }

    public void Wait(uint timeoutMilliseconds = uint.MaxValue)
    {
        ObjectDisposedException.ThrowIf(handle is null || handle.IsClosed, this);
        MkvCodecInfo.ThrowIfFailed(
            NativeMethods.mkvc_submission_wait(handle!, timeoutMilliseconds));
    }

    /// <summary>
    /// Asynchronously wait for terminal completion without blocking a managed
    /// worker thread. Disposing this lease concurrently is safe; the native
    /// handle remains retained until this operation exits.
    /// </summary>
    /// <param name="timeout">
    /// Maximum wait, or null/Timeout.InfiniteTimeSpan for no deadline.
    /// </param>
    /// <param name="cancellationToken">Cancels only this managed wait.</param>
    /// <exception cref="TimeoutException">The deadline elapsed while pending.</exception>
    /// <exception cref="OperationCanceledException">The token was cancelled.</exception>
    public async Task WaitAsync(TimeSpan? timeout = null,
        CancellationToken cancellationToken = default)
    {
        MkvSubmissionHandle nativeHandle = handle ??
            throw new ObjectDisposedException(nameof(MkvSubmission));
        if (nativeHandle.IsClosed)
            throw new ObjectDisposedException(nameof(MkvSubmission));
        if (timeout is { } finite && finite != Timeout.InfiniteTimeSpan &&
            finite < TimeSpan.Zero)
            throw new ArgumentOutOfRangeException(nameof(timeout));

        bool referenceAdded = false;
        nativeHandle.DangerousAddRef(ref referenceAdded);
        try
        {
            nint pointer = nativeHandle.DangerousGetHandle();
            long started = System.Diagnostics.Stopwatch.GetTimestamp();
            while (true)
            {
                cancellationToken.ThrowIfCancellationRequested();
                MkvCodecInfo.ThrowIfFailed(
                    NativeMethods.mkvc_submission_query_raw(pointer, out var status));
                if (status != MkvSubmissionStatus.Pending)
                {
                    MkvCodecInfo.ThrowIfFailed(
                        NativeMethods.mkvc_submission_wait_raw(pointer, 0));
                    return;
                }
                if (timeout is { } limit && limit != Timeout.InfiniteTimeSpan)
                {
                    TimeSpan elapsed = System.Diagnostics.Stopwatch.GetElapsedTime(started);
                    if (elapsed >= limit)
                        throw new TimeoutException("mkvcodec submission wait timed out");
                    TimeSpan remaining = limit - elapsed;
                    await Task.Delay(
                        remaining < TimeSpan.FromMilliseconds(10)
                            ? remaining : TimeSpan.FromMilliseconds(10),
                        cancellationToken).ConfigureAwait(false);
                }
                else
                {
                    await Task.Delay(10, cancellationToken).ConfigureAwait(false);
                }
            }
        }
        finally
        {
            if (referenceAdded) nativeHandle.DangerousRelease();
        }
    }

    public void Dispose()
    {
        handle?.Dispose(); // Native release waits if work is still pending.
        handle = null;
        GC.SuppressFinalize(this);
    }
}
