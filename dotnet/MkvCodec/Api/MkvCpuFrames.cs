namespace MkvCodec;

/// <summary>Owned decoded I420 planes.</summary>
public sealed record MkvI420Frame(uint Width, uint Height, long PtsNanoseconds,
    byte[] Y, byte[] U, byte[] V);

/// <summary>Owned decoded BGR pixels.</summary>
public sealed record MkvBgrFrame(uint Width, uint Height, long PtsNanoseconds,
    byte[] Pixels, int Stride);

/// <summary>Owned decoded RGB pixels.</summary>
public sealed record MkvRgbFrame(uint Width, uint Height, long PtsNanoseconds,
    byte[] Pixels, int Stride);

/// <summary>Owned decoded BGRA pixels.</summary>
public sealed record MkvBgraFrame(uint Width, uint Height, long PtsNanoseconds,
    byte[] Pixels, int Stride);

/// <summary>Owned decoded NV12 luma and interleaved chroma planes.</summary>
public sealed record MkvNv12Frame(uint Width, uint Height, long PtsNanoseconds,
    byte[] Y, byte[] UV);
