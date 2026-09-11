using System.Runtime.InteropServices;

namespace MkvCodec;

/// <summary>Managed IDisposable writer over the stable native encoder handle.</summary>
public sealed class MkvVideoWriter : IDisposable
{
    private MkvEncoderHandle? handle;
    private readonly uint width;
    private readonly uint height;
    private readonly uint queueSize;
    private MkvPipelineMetrics? finalMetrics;
    private MkvPipelineStageMetrics? finalStageMetrics;

    public MkvVideoWriter(string path, uint width, uint height,
        uint fpsNumerator = 30, uint fpsDenominator = 1,
        MkvCodecKind codec = MkvCodecKind.Vp9,
        MkvBackend backend = MkvBackend.Cpu, uint quality = 32,
        uint queueSize = 0, bool requireGpuResident = false)
    {
        if (requireGpuResident && backend == MkvBackend.Cpu)
            throw new ArgumentException(
                "GPU-resident encoding requires Intel or NVIDIA", nameof(backend));
        if (requireGpuResident && queueSize != 0)
            throw new ArgumentException(
                "GPU-resident encoding currently requires queueSize=0", nameof(queueSize));
        nint utf8 = Marshal.StringToCoTaskMemUTF8(path);
        try
        {
            var config = new NativeEncoderConfig {
                StructSize = checked((uint)Marshal.SizeOf<NativeEncoderConfig>()),
                StructVersion = 1, OutputPathUtf8 = utf8,
                Codec = (uint)codec, Backend = (uint)backend,
                Width = width, Height = height, FpsNum = fpsNumerator,
                FpsDen = fpsDenominator, Quality = quality,
                QueueSize = queueSize
            };
            MkvCodecInfo.ThrowIfFailed(
                NativeMethods.mkvc_encoder_create(ref config, out handle));
            if (requireGpuResident)
            {
                var policy = StrictGpuPolicy();
                try
                {
                    MkvCodecInfo.ThrowIfFailed(
                        NativeMethods.mkvc_encoder_set_copy_policy(handle!, ref policy));
                }
                catch
                {
                    handle?.Dispose();
                    handle = null;
                    throw;
                }
            }
        }
        finally { Marshal.FreeCoTaskMem(utf8); }
        this.width = width;
        this.height = height;
        this.queueSize = queueSize;
    }

    private static NativeCopyPolicy StrictGpuPolicy() => new() {
        StructSize = checked((uint)Marshal.SizeOf<NativeCopyPolicy>()),
        StructVersion = 1, RequireGpuResident = 1,
        AllowGpuCopy = 1, AllowCpuCopy = 0
    };

    public unsafe void WriteI420(byte[] y, byte[] u, byte[] v, long pts = -1)
    {
        ObjectDisposedException.ThrowIf(handle is null || handle.IsClosed, this);
        int ySize = checked((int)(width * height));
        int chromaSize = checked(ySize / 4);
        if (y.Length < ySize || u.Length < chromaSize || v.Length < chromaSize)
            throw new ArgumentException("I420 plane is smaller than the configured frame");
        fixed (byte* py = y)
        fixed (byte* pu = u)
        fixed (byte* pv = v)
        {
            var frame = new NativeFrameView {
                StructSize = checked((uint)Marshal.SizeOf<NativeFrameView>()),
                StructVersion = 1, PixelFormat = (uint)MkvPixelFormat.I420,
                Width = width, Height = height, Plane0 = (nint)py,
                Plane1 = (nint)pu, Plane2 = (nint)pv,
                Stride0 = checked((int)width), Stride1 = checked((int)width / 2),
                Stride2 = checked((int)width / 2), Pts = pts
            };
            MkvCodecInfo.ThrowIfFailed(
                NativeMethods.mkvc_encoder_write_frame(handle!, ref frame));
        }
    }

    /// <summary>Submit one NV12 frame after validating both managed planes.</summary>
    public unsafe void WriteNv12(byte[] y, byte[] uv, long pts = -1)
    {
        ObjectDisposedException.ThrowIf(handle is null || handle.IsClosed, this);
        int ySize = checked((int)(width * height));
        int uvSize = checked(ySize / 2);
        if (y.Length < ySize || uv.Length < uvSize)
            throw new ArgumentException("NV12 plane is smaller than the configured frame");
        fixed (byte* py = y)
        fixed (byte* puv = uv)
        {
            var frame = new NativeFrameView {
                StructSize = checked((uint)Marshal.SizeOf<NativeFrameView>()),
                StructVersion = 1, PixelFormat = (uint)MkvPixelFormat.Nv12,
                Width = width, Height = height, Plane0 = (nint)py,
                Plane1 = (nint)puv, Stride0 = checked((int)width),
                Stride1 = checked((int)width), Pts = pts
            };
            MkvCodecInfo.ThrowIfFailed(
                NativeMethods.mkvc_encoder_write_frame(handle!, ref frame));
        }
    }

    /// <summary>Submit one packed BGR frame using the supplied row stride.</summary>
    public void WriteBgr(byte[] pixels, int stride = 0, long pts = -1) =>
        WritePacked(pixels, MkvPixelFormat.Bgr24, 3, stride, pts);

    /// <summary>Submit one packed RGB frame using the supplied row stride.</summary>
    public void WriteRgb(byte[] pixels, int stride = 0, long pts = -1) =>
        WritePacked(pixels, MkvPixelFormat.Rgb24, 3, stride, pts);

    /// <summary>Submit one packed BGRA frame using the supplied row stride.</summary>
    public void WriteBgra(byte[] pixels, int stride = 0, long pts = -1) =>
        WritePacked(pixels, MkvPixelFormat.Bgra32, 4, stride, pts);

    private unsafe void WritePacked(byte[] pixels, MkvPixelFormat format,
        int channels, int stride, long pts)
    {
        ObjectDisposedException.ThrowIf(handle is null || handle.IsClosed, this);
        ArgumentNullException.ThrowIfNull(pixels);
        int rowBytes = checked((int)width * channels);
        if (stride == 0) stride = rowBytes;
        if (stride < rowBytes)
            throw new ArgumentOutOfRangeException(nameof(stride),
                "stride is smaller than one packed row");
        int required = checked(stride * (checked((int)height) - 1) + rowBytes);
        if (pixels.Length < required)
            throw new ArgumentException(
                "packed array is smaller than the configured frame", nameof(pixels));
        fixed (byte* pointer = pixels)
        {
            var frame = new NativeFrameView {
                StructSize = checked((uint)Marshal.SizeOf<NativeFrameView>()),
                StructVersion = 1, PixelFormat = (uint)format,
                Width = width, Height = height, Plane0 = (nint)pointer,
                Stride0 = stride, Pts = pts
            };
            MkvCodecInfo.ThrowIfFailed(
                NativeMethods.mkvc_encoder_write_frame(handle!, ref frame));
        }
    }

    /// <summary>
    /// Pins managed I420 arrays only for this synchronous native call. The
    /// codec has finished reading the input when this method returns.
    /// </summary>
    public unsafe void WriteBorrowedI420(
        byte[] y, byte[] u, byte[] v, long pts = -1)
    {
        ObjectDisposedException.ThrowIf(handle is null || handle.IsClosed, this);
        if (queueSize != 0)
            throw new InvalidOperationException(
                "Synchronous borrowed writes require queueSize=0");
        int ySize = checked((int)(width * height));
        int chromaSize = checked(ySize / 4);
        if (y.Length < ySize || u.Length < chromaSize || v.Length < chromaSize)
            throw new ArgumentException("I420 plane is smaller than the configured frame");
        fixed (byte* py = y)
        fixed (byte* pu = u)
        fixed (byte* pv = v)
        {
            var frame = new NativeFrameView {
                StructSize = checked((uint)Marshal.SizeOf<NativeFrameView>()),
                StructVersion = 1, PixelFormat = (uint)MkvPixelFormat.I420,
                Width = width, Height = height, Plane0 = (nint)py,
                Plane1 = (nint)pu, Plane2 = (nint)pv,
                Stride0 = checked((int)width), Stride1 = checked((int)width / 2),
                Stride2 = checked((int)width / 2), Pts = pts
            };
            MkvCodecInfo.ThrowIfFailed(
                NativeMethods.mkvc_encoder_write_frame_borrowed(handle!, ref frame));
        }
    }

    /// <summary>Submit a compatible leased GPU surface without CPU pixel transfer.</summary>
    public void WriteSurface(MkvGpuFrame frame)
    {
        ObjectDisposedException.ThrowIf(handle is null || handle.IsClosed, this);
        ArgumentNullException.ThrowIfNull(frame);
        MkvCodecInfo.ThrowIfFailed(NativeMethods.mkvc_encoder_write_gpu_frame(
            handle!, frame.BorrowHandle()));
    }

    /// <summary>Submit native pool memory without pinning a managed array.</summary>
    public MkvSubmission Submit(MkvCpuBuffer buffer, long pts = -1)
    {
        ObjectDisposedException.ThrowIf(handle is null || handle.IsClosed, this);
        ArgumentNullException.ThrowIfNull(buffer);
        if (queueSize == 0)
            throw new InvalidOperationException(
                "Native CPU buffer submission requires a positive queueSize");
        if (buffer.Width != width || buffer.Height != height)
            throw new ArgumentException(
                "CPU buffer dimensions do not match the writer", nameof(buffer));
        MkvCodecInfo.ThrowIfFailed(NativeMethods.mkvc_encoder_submit_cpu_buffer(
            handle!, buffer.BorrowHandle(), pts, out MkvSubmissionHandle submission));
        return new MkvSubmission(submission);
    }

    public void Flush()
    {
        ObjectDisposedException.ThrowIf(handle is null || handle.IsClosed, this);
        MkvCodecInfo.ThrowIfFailed(NativeMethods.mkvc_encoder_flush(handle));
    }

    /// <summary>Discard queued frames and wake blocked submissions.</summary>
    public void Cancel()
    {
        ObjectDisposedException.ThrowIf(handle is null || handle.IsClosed, this);
        MkvCodecInfo.ThrowIfFailed(NativeMethods.mkvc_encoder_cancel(handle!));
    }

    public MkvPipelineMetrics Metrics => finalMetrics ?? ReadMetrics();

    /// <summary>Exact frame, flush and close backend operation timings.</summary>
    public MkvPipelineStageMetrics StageMetrics => finalStageMetrics ?? ReadStageMetrics();

    private MkvPipelineStageMetrics ReadStageMetrics()
    {
        ObjectDisposedException.ThrowIf(handle is null || handle.IsClosed, this);
        var metrics = new MkvPipelineStageMetrics {
            StructSize = checked((uint)Marshal.SizeOf<MkvPipelineStageMetrics>()),
            StructVersion = 1
        };
        MkvCodecInfo.ThrowIfFailed(NativeMethods.mkvc_encoder_get_stage_metrics(
            handle!.DangerousGetHandle(), ref metrics));
        return metrics;
    }

    private MkvPipelineMetrics ReadMetrics()
    {
        ObjectDisposedException.ThrowIf(handle is null || handle.IsClosed, this);
        var metrics = new MkvPipelineMetrics {
            StructSize = checked((uint)Marshal.SizeOf<MkvPipelineMetrics>()),
            StructVersion = 1
        };
        MkvCodecInfo.ThrowIfFailed(
            NativeMethods.mkvc_encoder_get_metrics(handle!.DangerousGetHandle(), ref metrics));
        return metrics;
    }

    public void Dispose()
    {
        if (handle is null) return;
        if (!handle.IsClosed)
        {
            MkvResult result = NativeMethods.mkvc_encoder_close(handle.DangerousGetHandle());
            try {
                finalMetrics = ReadMetrics();
                finalStageMetrics = ReadStageMetrics();
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
