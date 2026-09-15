"""Architecture guards for the layered .NET SDK source tree."""

from __future__ import annotations

import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SDK = ROOT / "dotnet" / "MkvCodec"


class DotnetPackageLayoutTests(unittest.TestCase):
    """Keep managed API, native ABI, interop, and internal code separated."""

    def test_required_layers_exist(self) -> None:
        expected = {
            "Api/MkvCodecException.cs",
            "Api/MkvCodecInfo.cs",
            "Api/MkvCpuBuffer.cs",
            "Api/MkvCpuFramePool.cs",
            "Api/MkvCpuFrames.cs",
            "Api/MkvGpuInteropInfo.cs",
            "Api/MkvGpuResourcePool.cs",
            "Api/MkvGpuResourceReservation.cs",
            "Api/MkvVideoCapture.cs",
            "Api/MkvVideoWriter.cs",
            "Api/MkvSubmission.cs",
            "Interop/MkvGpuFrame.cs",
            "Internal/PipelineMetricsReader.cs",
            "Native/NativeMethods.cs",
            "Native/NativeMethods.Generated.cs",
            "Native/NativeTypes.Generated.cs",
            "Native/SafeHandles.cs",
        }
        missing = sorted(path for path in expected if not (SDK / path).is_file())
        self.assertEqual(missing, [])

    def test_project_root_contains_no_implementation_classes(self) -> None:
        sources = sorted(path.name for path in SDK.glob("*.cs"))
        self.assertEqual(sources, ["GlobalUsings.cs"])

    def test_native_handles_are_not_public_api(self) -> None:
        source = (SDK / "Native" / "SafeHandles.cs").read_text(encoding="utf-8")
        self.assertNotIn("public sealed class MkvEncoderHandle", source)
        self.assertNotIn("public sealed class MkvDecoderHandle", source)
        self.assertNotIn("public sealed class MkvGpuFrameHandle", source)
        self.assertIn("internal sealed class MkvEncoderHandle", source)

    def test_pipeline_metric_marshalling_is_centralized(self) -> None:
        for name in ("MkvVideoCapture.cs", "MkvVideoWriter.cs"):
            source = (SDK / "Api" / name).read_text(encoding="utf-8")
            self.assertNotIn("private MkvPipelineMetrics ReadMetrics", source)
            self.assertIn("PipelineMetricsReader", source)


if __name__ == "__main__":
    unittest.main()
