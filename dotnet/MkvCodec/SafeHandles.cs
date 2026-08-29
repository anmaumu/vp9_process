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
