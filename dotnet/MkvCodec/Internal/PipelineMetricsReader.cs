using System.Runtime.InteropServices;

namespace MkvCodec;

/// <summary>Centralizes versioned native pipeline-metric snapshots.</summary>
internal static class PipelineMetricsReader
{
    internal static MkvPipelineMetrics ReadMetrics(nint handle, bool encoder)
    {
        var value = new MkvPipelineMetrics {
            StructSize = checked((uint)Marshal.SizeOf<MkvPipelineMetrics>()),
            StructVersion = 1
        };
        MkvResult result = encoder
            ? NativeMethods.mkvc_encoder_get_metrics(handle, ref value)
            : NativeMethods.mkvc_decoder_get_metrics(handle, ref value);
        MkvCodecInfo.ThrowIfFailed(result);
        return value;
    }

    internal static MkvPipelineStageMetrics ReadStageMetrics(nint handle, bool encoder)
    {
        var value = new MkvPipelineStageMetrics {
            StructSize = checked((uint)Marshal.SizeOf<MkvPipelineStageMetrics>()),
            StructVersion = 1
        };
        MkvResult result = encoder
            ? NativeMethods.mkvc_encoder_get_stage_metrics(handle, ref value)
            : NativeMethods.mkvc_decoder_get_stage_metrics(handle, ref value);
        MkvCodecInfo.ThrowIfFailed(result);
        return value;
    }

    internal static MkvPipelineComponentMetrics ReadComponentMetrics(
        nint handle, bool encoder)
    {
        var value = new MkvPipelineComponentMetrics {
            StructSize = checked((uint)Marshal.SizeOf<MkvPipelineComponentMetrics>()),
            StructVersion = 1
        };
        MkvResult result = encoder
            ? NativeMethods.mkvc_encoder_get_component_metrics(handle, ref value)
            : NativeMethods.mkvc_decoder_get_component_metrics(handle, ref value);
        MkvCodecInfo.ThrowIfFailed(result);
        return value;
    }

    internal static MkvCopyEdgeMetrics ReadCopyEdgeMetrics(nint handle, bool encoder)
    {
        var value = new MkvCopyEdgeMetrics {
            StructSize = checked((uint)Marshal.SizeOf<MkvCopyEdgeMetrics>()),
            StructVersion = 1
        };
        MkvResult result = encoder
            ? NativeMethods.mkvc_encoder_get_copy_edge_metrics(handle, ref value)
            : NativeMethods.mkvc_decoder_get_copy_edge_metrics(handle, ref value);
        MkvCodecInfo.ThrowIfFailed(result);
        return value;
    }
}
