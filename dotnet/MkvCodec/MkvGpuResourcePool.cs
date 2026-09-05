using System.Runtime.InteropServices;

namespace MkvCodec;

/// <summary>
/// Fixed-capacity backpressure gate for caller-preallocated GPU resources.
/// Resource allocation remains in CUDA, oneAPI, D3D11 or VA-API application code.
/// </summary>
public sealed class MkvGpuResourcePool : IDisposable
{
    private MkvGpuResourcePoolHandle? handle;

    public MkvGpuResourcePool(uint capacity)
    {
        if (capacity == 0) throw new ArgumentOutOfRangeException(nameof(capacity));
        var config = new NativeGpuResourcePoolConfig {
            StructSize = checked((uint)Marshal.SizeOf<NativeGpuResourcePoolConfig>()),
            StructVersion = 1, Capacity = capacity
        };
        MkvCodecInfo.ThrowIfFailed(
            NativeMethods.mkvc_gpu_resource_pool_create(ref config, out handle));
    }

    public MkvGpuResourceReservation Acquire(uint timeoutMilliseconds = uint.MaxValue)
    {
        ObjectDisposedException.ThrowIf(handle is null || handle.IsClosed, this);
        MkvCodecInfo.ThrowIfFailed(NativeMethods.mkvc_gpu_resource_pool_acquire(
            handle!, timeoutMilliseconds, out var reservation));
        return new MkvGpuResourceReservation(reservation);
    }

    public bool TryAcquire(out MkvGpuResourceReservation? reservation)
    {
        ObjectDisposedException.ThrowIf(handle is null || handle.IsClosed, this);
        var result = NativeMethods.mkvc_gpu_resource_pool_acquire(
            handle!, 0, out var nativeReservation);
        if (result == MkvResult.WouldBlock)
        {
            reservation = null;
            return false;
        }
        MkvCodecInfo.ThrowIfFailed(result);
        reservation = new MkvGpuResourceReservation(nativeReservation);
        return true;
    }

    public MkvGpuResourcePoolStatistics Statistics
    {
        get
        {
            ObjectDisposedException.ThrowIf(handle is null || handle.IsClosed, this);
            var value = new MkvGpuResourcePoolStatistics {
                StructSize = checked((uint)Marshal.SizeOf<MkvGpuResourcePoolStatistics>()),
                StructVersion = 1
            };
            MkvCodecInfo.ThrowIfFailed(
                NativeMethods.mkvc_gpu_resource_pool_get_stats(handle!, ref value));
            return value;
        }
    }

    public void Dispose()
    {
        handle?.Dispose();
        handle = null;
        GC.SuppressFinalize(this);
    }
}

/// <summary>
/// Exclusive slot lease. It may be passed as a managed owner when importing a
/// GPU frame so slot reuse is deferred until all native/DLPack consumers finish.
/// </summary>
public sealed class MkvGpuResourceReservation : IDisposable
{
    private MkvGpuResourceReservationHandle? handle;

    internal MkvGpuResourceReservation(MkvGpuResourceReservationHandle handle)
    {
        this.handle = handle;
        var value = new MkvGpuResourceReservationDescriptor {
            StructSize = checked((uint)Marshal.SizeOf<MkvGpuResourceReservationDescriptor>()),
            StructVersion = 1
        };
        try
        {
            MkvCodecInfo.ThrowIfFailed(
                NativeMethods.mkvc_gpu_resource_reservation_get_desc(handle, ref value));
        }
        catch
        {
            handle.Dispose();
            this.handle = null;
            throw;
        }
        Descriptor = value;
    }

    public MkvGpuResourceReservationDescriptor Descriptor { get; }

    public void Dispose()
    {
        handle?.Dispose();
        handle = null;
        GC.SuppressFinalize(this);
    }
}
