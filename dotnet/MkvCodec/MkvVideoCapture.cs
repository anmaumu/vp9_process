using System.Runtime.InteropServices;

namespace MkvCodec;

public sealed record MkvI420Frame(uint Width, uint Height, long PtsNanoseconds,
    byte[] Y, byte[] U, byte[] V);

/// <summary>Managed IDisposable capture returning owned I420 arrays.</summary>
public sealed class MkvVideoCapture : IDisposable
{
    private MkvDecoderHandle? handle;
    private MkvPipelineMetrics? finalMetrics;

    public MkvVideoCapture(string path, MkvCodecKind codec = MkvCodecKind.Vp9,
        MkvBackend backend = MkvBackend.Cpu, uint prefetch = 0)
    {
        nint utf8 = Marshal.StringToCoTaskMemUTF8(path);
        try
        {
            var config = new NativeDecoderConfig {
                StructSize = checked((uint)Marshal.SizeOf<NativeDecoderConfig>()),
                StructVersion = 1, InputPathUtf8 = utf8,
                Codec = (uint)codec, Backend = (uint)backend, Prefetch = prefetch
            };
            MkvCodecInfo.ThrowIfFailed(
                NativeMethods.mkvc_decoder_create(ref config, out handle));
        }
        finally { Marshal.FreeCoTaskMem(utf8); }
    }

    public MkvI420Frame? ReadI420()
    {
        ObjectDisposedException.ThrowIf(handle is null || handle.IsClosed, this);
        MkvResult result = NativeMethods.mkvc_decoder_read(handle!, out MkvFrameHandle frame);
        if (result == MkvResult.EndOfStream) return null;
        MkvCodecInfo.ThrowIfFailed(result);
        using (frame)
        {
            var view = new NativeFrameView {
                StructSize = checked((uint)Marshal.SizeOf<NativeFrameView>()),
                StructVersion = 1
            };
            MkvCodecInfo.ThrowIfFailed(NativeMethods.mkvc_frame_get_view(frame, ref view));
            if (view.PixelFormat != (uint)MkvPixelFormat.I420)
                throw new InvalidOperationException("native decoder returned non-I420 data");
            byte[] y = CopyPlane(view.Plane0, view.Stride0, view.Width, view.Height);
            byte[] u = CopyPlane(view.Plane1, view.Stride1, view.Width / 2, view.Height / 2);
            byte[] v = CopyPlane(view.Plane2, view.Stride2, view.Width / 2, view.Height / 2);
            return new MkvI420Frame(view.Width, view.Height, view.Pts, y, u, v);
        }
    }

    /// <summary>Read one leased GPU surface without copying pixels to CPU memory.</summary>
    public MkvGpuFrame? ReadSurface()
    {
        ObjectDisposedException.ThrowIf(handle is null || handle.IsClosed, this);
        MkvResult result = NativeMethods.mkvc_decoder_read_gpu(
            handle!, out MkvGpuFrameHandle frame);
        if (result == MkvResult.EndOfStream) return null;
        MkvCodecInfo.ThrowIfFailed(result);
        return new MkvGpuFrame(frame);
    }

    private static byte[] CopyPlane(nint source, int stride, uint width, uint height)
    {
        byte[] result = new byte[checked((int)(width * height))];
        for (int row = 0; row < height; ++row)
            Marshal.Copy(source + checked(row * stride), result,
                checked(row * (int)width), checked((int)width));
        return result;
    }

    public MkvPipelineMetrics Metrics => finalMetrics ?? ReadMetrics();

    private MkvPipelineMetrics ReadMetrics()
    {
        ObjectDisposedException.ThrowIf(handle is null || handle.IsClosed, this);
        var metrics = new MkvPipelineMetrics {
            StructSize = checked((uint)Marshal.SizeOf<MkvPipelineMetrics>()),
            StructVersion = 1
        };
        MkvCodecInfo.ThrowIfFailed(
            NativeMethods.mkvc_decoder_get_metrics(handle!.DangerousGetHandle(), ref metrics));
        return metrics;
    }

    public void Dispose()
    {
        if (handle is null) return;
        if (!handle.IsClosed)
        {
            MkvResult result = NativeMethods.mkvc_decoder_close(handle.DangerousGetHandle());
            try { finalMetrics = ReadMetrics(); }
            finally
            {
                handle.Dispose();
                handle = null;
            }
            MkvCodecInfo.ThrowIfFailed(result);
        }
        GC.SuppressFinalize(this);
    }
}
