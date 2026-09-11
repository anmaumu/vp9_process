using System.Runtime.InteropServices;

namespace MkvCodec;

public sealed record MkvI420Frame(uint Width, uint Height, long PtsNanoseconds,
    byte[] Y, byte[] U, byte[] V);
public sealed record MkvBgrFrame(uint Width, uint Height, long PtsNanoseconds,
    byte[] Pixels, int Stride);

/// <summary>Managed IDisposable capture returning owned I420 or packed BGR arrays.</summary>
public sealed class MkvVideoCapture : IDisposable
{
    private MkvDecoderHandle? handle;
    private MkvPipelineMetrics? finalMetrics;
    private readonly uint conversionThreads;

    /// <summary>
    /// Open a decoder. prefetch controls bounded read-ahead, decodeThreads controls
    /// the codec, and conversionThreads independently controls large packed conversion.
    /// </summary>
    public MkvVideoCapture(string path, MkvCodecKind codec = MkvCodecKind.Auto,
        MkvBackend backend = MkvBackend.Cpu, uint prefetch = 0,
        bool requireGpuResident = false, uint decodeThreads = 0,
        uint conversionThreads = 0)
    {
        if (conversionThreads > 4)
            throw new ArgumentOutOfRangeException(nameof(conversionThreads),
                "conversionThreads must be between 0 and 4");
        if (requireGpuResident && backend == MkvBackend.Cpu)
            throw new ArgumentException(
                "GPU-resident decoding requires Intel or NVIDIA", nameof(backend));
        if (requireGpuResident && prefetch != 0)
            throw new ArgumentException(
                "GPU-resident decoding currently requires prefetch=0", nameof(prefetch));
        nint utf8 = Marshal.StringToCoTaskMemUTF8(path);
        try
        {
            var config = new NativeDecoderConfig {
                StructSize = checked((uint)Marshal.SizeOf<NativeDecoderConfig>()),
                StructVersion = 1, InputPathUtf8 = utf8,
                Codec = (uint)codec, Backend = (uint)backend,
                Threads = decodeThreads, Prefetch = prefetch
            };
            MkvCodecInfo.ThrowIfFailed(
                NativeMethods.mkvc_decoder_create(ref config, out handle));
            try
            {
                var info = new MkvVideoInfo {
                    StructSize = checked((uint)Marshal.SizeOf<MkvVideoInfo>()),
                    StructVersion = 1
                };
                MkvCodecInfo.ThrowIfFailed(NativeMethods.mkvc_decoder_get_info(
                    handle!.DangerousGetHandle(), ref info));
                Info = info;
                if (requireGpuResident)
                {
                    var policy = new NativeCopyPolicy {
                        StructSize = checked((uint)Marshal.SizeOf<NativeCopyPolicy>()),
                        StructVersion = 1, RequireGpuResident = 1,
                        AllowGpuCopy = 1, AllowCpuCopy = 0
                    };
                    MkvCodecInfo.ThrowIfFailed(
                        NativeMethods.mkvc_decoder_set_copy_policy(handle!, ref policy));
                }
            }
            catch
            {
                handle?.Dispose();
                handle = null;
                throw;
            }
        }
        finally { Marshal.FreeCoTaskMem(utf8); }
        this.conversionThreads = conversionThreads;
    }

    /// <summary>Immutable metadata for the selected input video track.</summary>
    public MkvVideoInfo Info { get; }

    public MkvCodecKind Codec => Info.Codec;
    public uint Width => Info.Width;
    public uint Height => Info.Height;
    public double? FramesPerSecond => Info.FpsKnown != 0 && Info.FpsDen != 0
        ? (double)Info.FpsNum / Info.FpsDen : null;
    public long? DurationNanoseconds => Info.DurationKnown != 0
        ? Info.DurationNanoseconds : null;
    public ulong? FrameCount => Info.FrameCountKnown != 0 ? Info.FrameCount : null;

    /// <summary>
    /// Read one owned packed BGR frame. Large color conversions use the capture's
    /// bounded conversionThreads setting independently of decoder concurrency.
    /// </summary>
    public unsafe MkvBgrFrame? ReadBgr()
    {
        ObjectDisposedException.ThrowIf(handle is null || handle.IsClosed, this);
        MkvResult result = NativeMethods.mkvc_decoder_read(handle!, out MkvFrameHandle frame);
        if (result == MkvResult.EndOfStream) return null;
        MkvCodecInfo.ThrowIfFailed(result);
        using (frame)
        {
            var source = new NativeFrameView {
                StructSize = checked((uint)Marshal.SizeOf<NativeFrameView>()),
                StructVersion = 1
            };
            MkvCodecInfo.ThrowIfFailed(NativeMethods.mkvc_frame_get_view(frame, ref source));
            int stride = checked((int)source.Width * 3);
            byte[] pixels = new byte[checked(stride * (int)source.Height)];
            fixed (byte* pointer = pixels)
            {
                var destination = new NativeMutableFrameView {
                    StructSize = checked((uint)Marshal.SizeOf<NativeMutableFrameView>()),
                    StructVersion = 1,
                    PixelFormat = MkvPixelFormat.Bgr24,
                    Width = source.Width,
                    Height = source.Height,
                    Plane0 = (nint)pointer,
                    Stride0 = stride
                };
                var options = new NativeFrameCopyOptions {
                    StructSize = checked((uint)Marshal.SizeOf<NativeFrameCopyOptions>()),
                    StructVersion = 1,
                    ConversionThreads = conversionThreads
                };
                MkvCodecInfo.ThrowIfFailed(
                    NativeMethods.mkvc_frame_copy_to_ex(frame, ref destination, ref options));
                return new MkvBgrFrame(source.Width, source.Height, destination.Pts,
                    pixels, stride);
            }
        }
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
