using System.Diagnostics;
using System.Text.Json;
using MkvCodec;

SoakOptions options = SoakOptions.Parse(args);
string outputPath = Path.Combine(
    Path.GetTempPath(), $"mkvcodec-dotnet-pool-soak-{Guid.NewGuid():N}.webm");
var process = Process.GetCurrentProcess();
var timer = Stopwatch.StartNew();
long peakPrivateBytes = 0;
long peakManagedBytes = 0;
long peakPinnedObjects = 0;
long frames = 0;
MkvCpuFramePoolStatistics poolStatistics = default;
string status = "failed";
string? failure = null;

ForceFullCollection();
ResourceSnapshot baseline = Snapshot(process);
ResourceSnapshot final = baseline;
peakPrivateBytes = baseline.PrivateBytes;
peakManagedBytes = baseline.ManagedBytes;
peakPinnedObjects = baseline.PinnedObjects;
int[] baselineCollections = CollectionCounts();
timer.Restart();

try
{
    using var pool = new MkvCpuFramePool(
        MkvPixelFormat.I420, 64, 48, options.Capacity, pageLocked: true);
    using var writer = new MkvVideoWriter(
        outputPath, 64, 48, queueSize: options.Capacity);
    var pending = new Queue<MkvSubmission>();

    while (timer.Elapsed < options.Duration || frames < options.MinimumFrames)
    {
        MkvCpuBuffer buffer = pool.Acquire(5000);
        buffer.GetPlane(0).Fill(checked((byte)(32 + frames % 192)));
        buffer.GetPlane(1).Fill(96);
        buffer.GetPlane(2).Fill(160);
        MkvSubmission submission = writer.Submit(buffer, frames);
        buffer.Dispose();
        pending.Enqueue(submission);
        ++frames;

        // Create moving managed objects while native submissions retain their slots.
        // No managed array is pinned by this path.
        for (int index = 0; index < options.PressureArraysPerFrame; ++index)
            GC.KeepAlive(new byte[options.PressureArrayBytes]);
        if (frames % options.GcIntervalFrames == 0)
            ForceFullCollection();

        while (pending.Count >= options.Capacity)
        {
            MkvSubmission completed = pending.Dequeue();
            await completed.WaitAsync(TimeSpan.FromSeconds(30));
            completed.Dispose();
        }
        ResourceSnapshot current = Snapshot(process);
        peakPrivateBytes = Math.Max(peakPrivateBytes, current.PrivateBytes);
        peakManagedBytes = Math.Max(peakManagedBytes, current.ManagedBytes);
        peakPinnedObjects = Math.Max(peakPinnedObjects, current.PinnedObjects);
    }

    while (pending.TryDequeue(out MkvSubmission? submission))
    {
        await submission.WaitAsync(TimeSpan.FromSeconds(30));
        submission.Dispose();
    }
    writer.Flush();
    poolStatistics = pool.Statistics;
    if (poolStatistics.InUse != 0)
        throw new InvalidOperationException("CPU pool still owns slots after all submissions completed");
    if (poolStatistics.PeakInUse > options.Capacity ||
        poolStatistics.MemoryMode != MkvCpuMemoryMode.PageLocked)
        throw new InvalidOperationException("CPU pool bounds or memory mode changed during soak");
    if (poolStatistics.PeakInUse < options.MinimumPeakInUse)
        throw new InvalidOperationException(
            $"CPU pool peak occupancy {poolStatistics.PeakInUse} did not reach " +
            $"the required {options.MinimumPeakInUse}");
    status = "passed";
}
catch (Exception error)
{
    failure = error.ToString();
}
finally
{
    ForceFullCollection();
    final = Snapshot(process);
    peakPrivateBytes = Math.Max(peakPrivateBytes, final.PrivateBytes);
    peakManagedBytes = Math.Max(peakManagedBytes, final.ManagedBytes);
    peakPinnedObjects = Math.Max(peakPinnedObjects, final.PinnedObjects);
    if (File.Exists(outputPath)) File.Delete(outputPath);
}

long managedGrowth = final.ManagedBytes - baseline.ManagedBytes;
long privateGrowth = final.PrivateBytes - baseline.PrivateBytes;
long pinnedGrowth = final.PinnedObjects - baseline.PinnedObjects;
if (status == "passed" && managedGrowth > options.MaxManagedGrowthBytes)
{
    status = "failed";
    failure = $"managed heap grew by {managedGrowth} bytes";
}
if (status == "passed" && privateGrowth > options.MaxPrivateGrowthBytes)
{
    status = "failed";
    failure = $"private memory grew by {privateGrowth} bytes";
}
if (status == "passed" && pinnedGrowth > options.MaxPinnedObjectGrowth)
{
    status = "failed";
    failure = $"managed pinned-object count grew by {pinnedGrowth}";
}

int[] finalCollections = CollectionCounts();
var report = new
{
    schema_version = 1,
    status,
    failure,
    duration_seconds = timer.Elapsed.TotalSeconds,
    frames,
    page_locked = true,
    capacity = options.Capacity,
    minimum_peak_in_use = options.MinimumPeakInUse,
    gc = new
    {
        collections = new[] {
            finalCollections[0] - baselineCollections[0],
            finalCollections[1] - baselineCollections[1],
            finalCollections[2] - baselineCollections[2]
        },
        managed_bytes = new
        {
            baseline = baseline.ManagedBytes,
            peak = peakManagedBytes,
            final = final.ManagedBytes,
            growth = managedGrowth,
            limit = options.MaxManagedGrowthBytes
        },
        pinned_objects = new
        {
            baseline = baseline.PinnedObjects,
            peak = peakPinnedObjects,
            final = final.PinnedObjects,
            growth = pinnedGrowth,
            limit = options.MaxPinnedObjectGrowth
        }
    },
    process = new
    {
        private_bytes = new
        {
            baseline = baseline.PrivateBytes,
            peak = peakPrivateBytes,
            final = final.PrivateBytes,
            growth = privateGrowth,
            limit = options.MaxPrivateGrowthBytes
        }
    },
    pool = new
    {
        capacity = poolStatistics.Capacity,
        in_use = poolStatistics.InUse,
        peak_in_use = poolStatistics.PeakInUse,
        allocation_bytes = poolStatistics.AllocationBytes,
        page_locked_bytes = poolStatistics.PageLockedBytes,
        acquisitions = poolStatistics.Acquisitions,
        rejected_acquisitions = poolStatistics.RejectedAcquisitions,
        wait_nanoseconds = poolStatistics.WaitNanoseconds,
        lease_time_nanoseconds = poolStatistics.LeaseTimeNanoseconds,
        peak_lease_time_nanoseconds = poolStatistics.PeakLeaseTimeNanoseconds
    }
};
string json = JsonSerializer.Serialize(report, new JsonSerializerOptions { WriteIndented = true });
if (options.ReportPath is not null)
{
    string fullReportPath = Path.GetFullPath(options.ReportPath);
    Directory.CreateDirectory(Path.GetDirectoryName(fullReportPath)!);
    File.WriteAllText(fullReportPath, json + Environment.NewLine);
}
Console.WriteLine(json);
return status == "passed" ? 0 : 1;

static void ForceFullCollection()
{
    GC.Collect(GC.MaxGeneration, GCCollectionMode.Forced, blocking: true, compacting: true);
    GC.WaitForPendingFinalizers();
    GC.Collect(GC.MaxGeneration, GCCollectionMode.Forced, blocking: true, compacting: true);
}

static int[] CollectionCounts() =>
    [GC.CollectionCount(0), GC.CollectionCount(1), GC.CollectionCount(2)];

static ResourceSnapshot Snapshot(Process process)
{
    process.Refresh();
    GCMemoryInfo gc = GC.GetGCMemoryInfo();
    return new ResourceSnapshot(
        GC.GetTotalMemory(forceFullCollection: false),
        process.PrivateMemorySize64,
        gc.PinnedObjectsCount);
}

readonly record struct ResourceSnapshot(
    long ManagedBytes, long PrivateBytes, long PinnedObjects);

sealed record SoakOptions(
    TimeSpan Duration, uint Capacity, uint MinimumPeakInUse, long MinimumFrames,
    int PressureArraysPerFrame, int PressureArrayBytes, long GcIntervalFrames,
    long MaxManagedGrowthBytes, long MaxPrivateGrowthBytes,
    long MaxPinnedObjectGrowth, string? ReportPath)
{
    public static SoakOptions Parse(string[] arguments)
    {
        var values = new Dictionary<string, string>(StringComparer.Ordinal);
        for (int index = 0; index < arguments.Length; index += 2)
        {
            if (index + 1 >= arguments.Length || !arguments[index].StartsWith("--"))
                throw new ArgumentException("Arguments must be --name value pairs");
            values[arguments[index][2..]] = arguments[index + 1];
        }
        T Read<T>(string name, T fallback) where T : IParsable<T> =>
            values.TryGetValue(name, out string? text)
                ? T.Parse(text, null) : fallback;
        double durationSeconds = Read("duration-seconds", 2.0);
        uint capacity = Read("capacity", 4U);
        uint minimumPeakInUse = Read("minimum-peak-in-use", 1U);
        long minimumFrames = Read("minimum-frames", 16L);
        int pressureArrays = Read("pressure-arrays-per-frame", 8);
        int pressureBytes = Read("pressure-array-bytes", 128 * 1024);
        long gcInterval = Read("gc-interval-frames", 4L);
        long maxManagedGrowth = Read("max-managed-growth-bytes", 16L * 1024 * 1024);
        long maxPrivateGrowth = Read("max-private-growth-bytes", 64L * 1024 * 1024);
        long maxPinnedGrowth = Read("max-pinned-object-growth", 0L);
        if (durationSeconds <= 0 || capacity == 0 || minimumPeakInUse == 0 ||
            minimumPeakInUse > capacity || minimumFrames <= 0 ||
            pressureArrays < 0 || pressureBytes <= 0 || gcInterval <= 0 ||
            maxManagedGrowth < 0 || maxPrivateGrowth < 0 || maxPinnedGrowth < 0)
            throw new ArgumentOutOfRangeException(nameof(arguments), "Soak values must be positive");
        return new SoakOptions(
            TimeSpan.FromSeconds(durationSeconds), capacity, minimumPeakInUse, minimumFrames,
            pressureArrays, pressureBytes, gcInterval,
            maxManagedGrowth, maxPrivateGrowth, maxPinnedGrowth,
            values.GetValueOrDefault("report"));
    }
}
