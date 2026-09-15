using System.Runtime.InteropServices;

namespace MkvCodec;

/// <summary>Managed IDisposable capture returning owned I420 or packed BGR arrays.</summary>
public sealed class MkvVideoCapture : IDisposable
{
    private MkvDecoderHandle? handle;
    private MkvPipelineMetrics? finalMetrics;
    private MkvPipelineStageMetrics? finalStageMetrics;
    private MkvPipelineComponentMetrics? finalComponentMetrics;
    private MkvCopyEdgeMetrics? finalCopyEdgeMetrics;
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
    public MkvBgrFrame? ReadBgr()
    {
        var frame = ReadPacked(MkvPixelFormat.Bgr24, 3);
        return frame is null ? null : new MkvBgrFrame(
            frame.Value.Width, frame.Value.Height, frame.Value.Pts,
            frame.Value.Pixels, frame.Value.Stride);
    }

    /// <summary>Read one owned packed RGB frame.</summary>
    public MkvRgbFrame? ReadRgb()
    {
        var frame = ReadPacked(MkvPixelFormat.Rgb24, 3);
        return frame is null ? null : new MkvRgbFrame(
            frame.Value.Width, frame.Value.Height, frame.Value.Pts,
            frame.Value.Pixels, frame.Value.Stride);
    }

    /// <summary>Read one owned packed BGRA frame.</summary>
    public MkvBgraFrame? ReadBgra()
    {
        var frame = ReadPacked(MkvPixelFormat.Bgra32, 4);
        return frame is null ? null : new MkvBgraFrame(
            frame.Value.Width, frame.Value.Height, frame.Value.Pts,
            frame.Value.Pixels, frame.Value.Stride);
    }

    private unsafe (uint Width, uint Height, long Pts, byte[] Pixels, int Stride)?
        ReadPacked(MkvPixelFormat format, int channels)
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
            int stride = checked((int)source.Width * channels);
            byte[] pixels = new byte[checked(stride * (int)source.Height)];
            fixed (byte* pointer = pixels)
            {
                var destination = new NativeMutableFrameView {
                    StructSize = checked((uint)Marshal.SizeOf<NativeMutableFrameView>()),
                    StructVersion = 1,
                    PixelFormat = format,
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
                return (source.Width, source.Height, destination.Pts, pixels, stride);
            }
        }
    }

    /// <summary>Read one owned NV12 frame with separate luma and UV arrays.</summary>
    public unsafe MkvNv12Frame? ReadNv12()
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
            byte[] y = new byte[checked((int)(source.Width * source.Height))];
            byte[] uv = new byte[checked(y.Length / 2)];
            fixed (byte* py = y)
            fixed (byte* puv = uv)
            {
                var destination = new NativeMutableFrameView {
                    StructSize = checked((uint)Marshal.SizeOf<NativeMutableFrameView>()),
                    StructVersion = 1, PixelFormat = MkvPixelFormat.Nv12,
                    Width = source.Width, Height = source.Height,
                    Plane0 = (nint)py, Plane1 = (nint)puv,
                    Stride0 = checked((int)source.Width),
                    Stride1 = checked((int)source.Width)
                };
                var options = new NativeFrameCopyOptions {
                    StructSize = checked((uint)Marshal.SizeOf<NativeFrameCopyOptions>()),
                    StructVersion = 1, ConversionThreads = conversionThreads
                };
                MkvCodecInfo.ThrowIfFailed(
                    NativeMethods.mkvc_frame_copy_to_ex(frame, ref destination, ref options));
                return new MkvNv12Frame(source.Width, source.Height,
                    destination.Pts, y, uv);
            }
        }
    }

    public unsafe MkvI420Frame? ReadI420()
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
            byte[] y = new byte[checked((int)(view.Width * view.Height))];
            byte[] u = new byte[checked(y.Length / 4)];
            byte[] v = new byte[u.Length];
            fixed (byte* py = y)
            fixed (byte* pu = u)
            fixed (byte* pv = v)
            {
                var destination = new NativeMutableFrameView {
                    StructSize = checked((uint)Marshal.SizeOf<NativeMutableFrameView>()),
                    StructVersion = 1, PixelFormat = MkvPixelFormat.I420,
                    Width = view.Width, Height = view.Height,
                    Plane0 = (nint)py, Plane1 = (nint)pu, Plane2 = (nint)pv,
                    Stride0 = checked((int)view.Width),
                    Stride1 = checked((int)view.Width / 2),
                    Stride2 = checked((int)view.Width / 2)
                };
                MkvCodecInfo.ThrowIfFailed(
                    NativeMethods.mkvc_frame_copy_to(frame, ref destination));
                return new MkvI420Frame(view.Width, view.Height,
                    destination.Pts, y, u, v);
            }
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

    public MkvPipelineMetrics Metrics => finalMetrics ??
        PipelineMetricsReader.ReadMetrics(BorrowHandle(), false);

    /// <summary>Exact backend operation timings split by caller and worker execution.</summary>
    public MkvPipelineStageMetrics StageMetrics => finalStageMetrics ??
        PipelineMetricsReader.ReadStageMetrics(BorrowHandle(), false);

    /// <summary>Exclusive conversion, codec, container and GPU-wait host timings.</summary>
    public MkvPipelineComponentMetrics ComponentMetrics =>
        finalComponentMetrics ?? PipelineMetricsReader.ReadComponentMetrics(
            BorrowHandle(), false);

    /// <summary>Copy/share operations observed at mkvcodec-controlled boundaries.</summary>
    public MkvCopyEdgeMetrics CopyEdgeMetrics =>
        finalCopyEdgeMetrics ?? PipelineMetricsReader.ReadCopyEdgeMetrics(
            BorrowHandle(), false);

    private nint BorrowHandle()
    {
        ObjectDisposedException.ThrowIf(handle is null || handle.IsClosed, this);
        return handle!.DangerousGetHandle();
    }

    public void Dispose()
    {
        if (handle is null) return;
        if (!handle.IsClosed)
        {
            MkvResult result = NativeMethods.mkvc_decoder_close(handle.DangerousGetHandle());
            try {
                nint nativeHandle = handle.DangerousGetHandle();
                finalMetrics = PipelineMetricsReader.ReadMetrics(nativeHandle, false);
                finalStageMetrics = PipelineMetricsReader.ReadStageMetrics(nativeHandle, false);
                finalComponentMetrics = PipelineMetricsReader.ReadComponentMetrics(
                    nativeHandle, false);
                finalCopyEdgeMetrics = PipelineMetricsReader.ReadCopyEdgeMetrics(
                    nativeHandle, false);
            }
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
