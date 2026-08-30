using Microsoft.Win32.SafeHandles;

namespace MkvCodec;

public sealed class MkvEncoderHandle : SafeHandleZeroOrMinusOneIsInvalid
{
    private MkvEncoderHandle() : base(true) { }
    protected override bool ReleaseHandle()
    {
        _ = NativeMethods.mkvc_encoder_close(handle);
        NativeMethods.mkvc_encoder_destroy(handle);
        return true;
    }
}

public sealed class MkvDecoderHandle : SafeHandleZeroOrMinusOneIsInvalid
{
    private MkvDecoderHandle() : base(true) { }
    protected override bool ReleaseHandle()
    {
        _ = NativeMethods.mkvc_decoder_close(handle);
        NativeMethods.mkvc_decoder_destroy(handle);
        return true;
    }
}

public sealed class MkvFrameHandle : SafeHandleZeroOrMinusOneIsInvalid
{
    private MkvFrameHandle() : base(true) { }
    protected override bool ReleaseHandle()
    {
        NativeMethods.mkvc_frame_release(handle);
        return true;
    }
}

public sealed class MkvGpuFrameHandle : SafeHandleZeroOrMinusOneIsInvalid
{
    private MkvGpuFrameHandle() : base(true) { }
    protected override bool ReleaseHandle()
    {
        NativeMethods.mkvc_gpu_frame_release(handle);
        return true;
    }
}

public sealed class MkvCpuFramePoolHandle : SafeHandleZeroOrMinusOneIsInvalid
{
    private MkvCpuFramePoolHandle() : base(true) { }
    protected override bool ReleaseHandle()
    {
        NativeMethods.mkvc_cpu_frame_pool_destroy(handle);
        return true;
    }
}

public sealed class MkvCpuBufferHandle : SafeHandleZeroOrMinusOneIsInvalid
{
    private MkvCpuBufferHandle() : base(true) { }
    protected override bool ReleaseHandle()
    {
        NativeMethods.mkvc_cpu_buffer_release(handle);
        return true;
    }
}

public sealed class MkvSubmissionHandle : SafeHandleZeroOrMinusOneIsInvalid
{
    private MkvSubmissionHandle() : base(true) { }
    protected override bool ReleaseHandle()
    {
        NativeMethods.mkvc_submission_release(handle);
        return true;
    }
}
