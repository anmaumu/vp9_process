using System.Runtime.InteropServices;

namespace MkvCodec;

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
