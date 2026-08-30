using System.Runtime.InteropServices;
using System.Runtime.CompilerServices;

namespace MkvCodec;

/// <summary>Safe lease over an Intel or NVIDIA backend-owned GPU surface.</summary>
public sealed class MkvGpuFrame : IDisposable
{
    private MkvGpuFrameHandle? handle;

    internal MkvGpuFrame(MkvGpuFrameHandle handle) => this.handle = handle;

    private sealed class ExternalOwner
    {
        internal required object Owner;
        internal Func<bool>? Query;
        internal Action<object>? Release;
    }

    /// <summary>
    /// Imports a process-local GPU resource. The managed owner remains rooted
    /// until native producer/consumer work and every frame lease have finished.
    /// The query may run on any native thread and must be thread-safe.
    /// </summary>
    public static unsafe MkvGpuFrame ImportExternal(
        MkvGpuFrameDescriptor descriptor,
        MkvGpuNativeHandleDescriptor nativeHandle,
        object owner,
        Func<bool>? producerReady = null,
        Action<object>? release = null)
    {
        ArgumentNullException.ThrowIfNull(owner);
        if (descriptor.PlaneOffsets is null || descriptor.PlaneOffsets.Length != 4 ||
            descriptor.Pitches is null || descriptor.Pitches.Length != 4)
            throw new ArgumentException("GPU descriptor arrays must contain four values",
                nameof(descriptor));
        if (nativeHandle.Handles is null || nativeHandle.Handles.Length != 4)
            throw new ArgumentException("Native handle array must contain four values",
                nameof(nativeHandle));
        descriptor.StructSize = checked((uint)Marshal.SizeOf<MkvGpuFrameDescriptor>());
        descriptor.StructVersion = 1;
        nativeHandle.StructSize =
            checked((uint)Marshal.SizeOf<MkvGpuNativeHandleDescriptor>());
        nativeHandle.StructVersion = 1;
        var state = new ExternalOwner {
            Owner = owner, Query = producerReady, Release = release
        };
        GCHandle root = GCHandle.Alloc(state);
        var config = new NativeGpuExternalFrameConfig {
            StructSize = checked((uint)Marshal.SizeOf<NativeGpuExternalFrameConfig>()),
            StructVersion = 1, Frame = descriptor, NativeHandle = nativeHandle,
            Query = producerReady is null ? nint.Zero :
                (nint)(delegate* unmanaged[Cdecl]<nint, uint*, MkvResult>)&QueryExternal,
            Release =
                (nint)(delegate* unmanaged[Cdecl]<nint, void>)&ReleaseExternal,
            UserData = GCHandle.ToIntPtr(root)
        };
        try
        {
            MkvCodecInfo.ThrowIfFailed(
                NativeMethods.mkvc_gpu_frame_import_external(ref config, out var frame));
            return new MkvGpuFrame(frame);
        }
        catch
        {
            root.Free();
            throw;
        }
    }

    /// <summary>
    /// Imports an NVIDIA CUDA-pointer frame and tracks its producer through the
    /// CUevent stored in nativeHandle.Handles[3], without synchronizing a device.
    /// The CUDA context is stored in Handles[1].
    /// </summary>
    public static unsafe MkvGpuFrame ImportCudaEvent(
        MkvGpuFrameDescriptor descriptor,
        MkvGpuNativeHandleDescriptor nativeHandle,
        object owner,
        Action<object>? release = null)
    {
        ArgumentNullException.ThrowIfNull(owner);
        if (descriptor.PlaneOffsets is null || descriptor.PlaneOffsets.Length != 4 ||
            descriptor.Pitches is null || descriptor.Pitches.Length != 4)
            throw new ArgumentException("GPU descriptor arrays must contain four values",
                nameof(descriptor));
        if (nativeHandle.Handles is null || nativeHandle.Handles.Length != 4 ||
            nativeHandle.Handles[3] == 0)
            throw new ArgumentException("CUDA event handle must be stored in Handles[3]",
                nameof(nativeHandle));
        descriptor.StructSize = checked((uint)Marshal.SizeOf<MkvGpuFrameDescriptor>());
        descriptor.StructVersion = 1;
        nativeHandle.StructSize =
            checked((uint)Marshal.SizeOf<MkvGpuNativeHandleDescriptor>());
        nativeHandle.StructVersion = 1;
        var state = new ExternalOwner { Owner = owner, Release = release };
        GCHandle root = GCHandle.Alloc(state);
        var config = new NativeGpuExternalFrameConfig {
            StructSize = checked((uint)Marshal.SizeOf<NativeGpuExternalFrameConfig>()),
            StructVersion = 1, Frame = descriptor, NativeHandle = nativeHandle,
            Query = nint.Zero,
            Release = (nint)(delegate* unmanaged[Cdecl]<nint, void>)&ReleaseExternal,
            UserData = GCHandle.ToIntPtr(root)
        };
        try
        {
            MkvCodecInfo.ThrowIfFailed(
                NativeMethods.mkvc_gpu_frame_import_cuda_event(ref config, out var frame));
            return new MkvGpuFrame(frame);
        }
        catch
        {
            root.Free();
            throw;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static unsafe MkvResult QueryExternal(nint opaque, uint* complete)
    {
        if (opaque == nint.Zero || complete == null) return MkvResult.InvalidArgument;
        try
        {
            var state = (ExternalOwner?)GCHandle.FromIntPtr(opaque).Target;
            if (state?.Query is null) return MkvResult.InvalidState;
            *complete = state.Query() ? 1u : 0u;
            return MkvResult.Ok;
        }
        catch { return MkvResult.Internal; }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void ReleaseExternal(nint opaque)
    {
        if (opaque == nint.Zero) return;
        GCHandle root = GCHandle.FromIntPtr(opaque);
        try
        {
            if (root.Target is ExternalOwner state)
            {
                if (state.Release is not null) state.Release(state.Owner);
                else if (state.Owner is IDisposable disposable) disposable.Dispose();
            }
        }
        catch { }
        finally { root.Free(); }
    }

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
