using MkvCodec;

if (args.Length != 4)
    throw new ArgumentException("usage: input output backend output-codec");

var input = args[0];
var output = args[1];
var backend = Enum.Parse<MkvBackend>(args[2], ignoreCase: true);
var outputCodec = Enum.Parse<MkvCodecKind>(args[3], ignoreCase: true);
var capabilities = MkvCodecInfo.GetCapabilities();
bool Supports(MkvCodecKind codec, bool encode) => capabilities.Any(item =>
    item.Backend == backend && item.Codec == codec &&
    (encode ? item.CanEncode : item.CanDecode) != 0);
if (!Supports(MkvCodecKind.Vp9, false) || !Supports(outputCodec, true))
    return 77;

try
{
    var info = MkvCodecInfo.ProbeVideo(input);
    File.Delete(output);
    ulong frames = 0;
    using var capture = new MkvVideoCapture(input, MkvCodecKind.Vp9, backend,
        prefetch: 0, requireGpuResident: true);
    using var writer = new MkvVideoWriter(output, info.Width, info.Height,
        info.FpsKnown != 0 ? info.FpsNum : 30,
        info.FpsKnown != 0 ? info.FpsDen : 1,
        outputCodec, backend, queueSize: 0, requireGpuResident: true);
    while (capture.ReadSurface() is { } frame)
    {
        using (frame)
        {
            if (frame.Descriptor.Backend != backend)
                throw new InvalidOperationException("GPU frame backend mismatch");
            writer.WriteSurface(frame);
            ++frames;
        }
    }
    writer.Flush();
    if (frames != info.FrameCount)
        throw new InvalidOperationException($"frame count mismatch: {frames} != {info.FrameCount}");
    if (capture.Metrics.CopyPath != 2 || writer.Metrics.CopyPath != 2)
        throw new InvalidOperationException("strict GPU path did not report zero-copy");
    if (!File.Exists(output) || new FileInfo(output).Length == 0)
        throw new InvalidOperationException("GPU output was not created");
    return 0;
}
catch (MkvCodecException error) when (error.Result == MkvResult.NotSupported)
{
    Console.Error.WriteLine(error.Message);
    return 77;
}
