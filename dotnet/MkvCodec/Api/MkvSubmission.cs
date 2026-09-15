namespace MkvCodec;

/// <summary>Completion lease for one asynchronously submitted native CPU buffer.</summary>
public sealed class MkvSubmission : IDisposable
{
    private MkvSubmissionHandle? handle;
    internal MkvSubmission(MkvSubmissionHandle handle) => this.handle = handle;

    public MkvSubmissionStatus Status
    {
        get
        {
            ObjectDisposedException.ThrowIf(handle is null || handle.IsClosed, this);
            MkvCodecInfo.ThrowIfFailed(
                NativeMethods.mkvc_submission_query(handle!, out MkvSubmissionStatus status));
            return status;
        }
    }

    public void Wait(uint timeoutMilliseconds = uint.MaxValue)
    {
        ObjectDisposedException.ThrowIf(handle is null || handle.IsClosed, this);
        MkvCodecInfo.ThrowIfFailed(
            NativeMethods.mkvc_submission_wait(handle!, timeoutMilliseconds));
    }

    /// <summary>
    /// Asynchronously wait for terminal completion without blocking a managed
    /// worker thread. Disposing this lease concurrently is safe; the native
    /// handle remains retained until this operation exits.
    /// </summary>
    /// <param name="timeout">
    /// Maximum wait, or null/Timeout.InfiniteTimeSpan for no deadline.
    /// </param>
    /// <param name="cancellationToken">Cancels only this managed wait.</param>
    /// <exception cref="TimeoutException">The deadline elapsed while pending.</exception>
    /// <exception cref="OperationCanceledException">The token was cancelled.</exception>
    public async Task WaitAsync(TimeSpan? timeout = null,
        CancellationToken cancellationToken = default)
    {
        MkvSubmissionHandle nativeHandle = handle ??
            throw new ObjectDisposedException(nameof(MkvSubmission));
        if (nativeHandle.IsClosed)
            throw new ObjectDisposedException(nameof(MkvSubmission));
        if (timeout is { } finite && finite != Timeout.InfiniteTimeSpan &&
            finite < TimeSpan.Zero)
            throw new ArgumentOutOfRangeException(nameof(timeout));

        bool referenceAdded = false;
        nativeHandle.DangerousAddRef(ref referenceAdded);
        try
        {
            nint pointer = nativeHandle.DangerousGetHandle();
            long started = System.Diagnostics.Stopwatch.GetTimestamp();
            while (true)
            {
                cancellationToken.ThrowIfCancellationRequested();
                MkvCodecInfo.ThrowIfFailed(
                    NativeMethods.mkvc_submission_query_raw(pointer, out var status));
                if (status != MkvSubmissionStatus.Pending)
                {
                    MkvCodecInfo.ThrowIfFailed(
                        NativeMethods.mkvc_submission_wait_raw(pointer, 0));
                    return;
                }
                if (timeout is { } limit && limit != Timeout.InfiniteTimeSpan)
                {
                    TimeSpan elapsed = System.Diagnostics.Stopwatch.GetElapsedTime(started);
                    if (elapsed >= limit)
                        throw new TimeoutException("mkvcodec submission wait timed out");
                    TimeSpan remaining = limit - elapsed;
                    await Task.Delay(
                        remaining < TimeSpan.FromMilliseconds(10)
                            ? remaining : TimeSpan.FromMilliseconds(10),
                        cancellationToken).ConfigureAwait(false);
                }
                else
                {
                    await Task.Delay(10, cancellationToken).ConfigureAwait(false);
                }
            }
        }
        finally
        {
            if (referenceAdded) nativeHandle.DangerousRelease();
        }
    }

    public void Dispose()
    {
        handle?.Dispose(); // Native release waits if work is still pending.
        handle = null;
        GC.SuppressFinalize(this);
    }
}
