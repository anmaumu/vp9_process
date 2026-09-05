namespace MkvCodec;

/// <summary>Normalized adapter information for one already-created GPU frame.</summary>
public sealed record MkvGpuInteropInfo(
    MkvBackend Backend,
    MkvGpuMemoryType MemoryType,
    MkvGpuNativeHandleType NativeHandleType,
    IReadOnlyList<string> ProcessingInterfaces,
    string Completion);
