"""Pure-Python acceptance tests for the vendor-neutral processor contract."""

from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "python" / "mkvcodec" / "api" / "processor.py"
SPEC = importlib.util.spec_from_file_location("mkvc_processor_contract", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
processor_api = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = processor_api
SPEC.loader.exec_module(processor_api)


class FakeInterop:
    def __init__(self, backend: str) -> None:
        self.backend = backend


class FakeFrame:
    def __init__(self, backend: str) -> None:
        self.interop = FakeInterop(backend)
        self.descriptor = {
            "device_id": 3,
            "width": 64,
            "height": 48,
            "pts_ns": 1234,
        }
        self.retain_count = 0
        self.close_count = 0

    def retain(self) -> "FakeFrame":
        self.retain_count += 1
        retained = FakeFrame(self.interop.backend)
        retained.close = self._close_retained
        return retained

    def _close_retained(self) -> None:
        self.close_count += 1

    def close(self) -> None:
        self.close_count += 1


class FakeProvider:
    def __dlpack_device__(self) -> tuple[int, int]:
        return 2, 3

    def __dlpack__(self, **arguments: object) -> object:
        return ("capsule", arguments)


class FakeAdapter:
    def __init__(self, backend: str) -> None:
        self.name = f"fake-{backend}"
        self.backends = (backend,)
        self.release_count = 0

    def supports(self, source: object, request: object) -> bool:
        return True

    def convert(self, source: FakeFrame, request: object) -> object:
        channels = 3 if request.format in ("rgb", "bgr") else 4
        shape = (48, 64, channels) if request.layout == "hwc" else (channels, 48, 64)
        info = processor_api.GpuImageInfo(
            backend=source.interop.backend,
            device_id=3,
            width=64,
            height=48,
            format=request.format,
            layout=request.layout,
            dtype=request.dtype,
            shape=shape,
            color_space=request.color_space,
            color_range=request.color_range,
            pts_ns=1234,
            adapter=self.name,
            completion="stream_event",
            copy_path="gpu_copy",
        )
        return processor_api.GpuImage(
            FakeProvider(), info, release=self._release
        )

    def _release(self) -> None:
        self.release_count += 1


class GpuProcessorContractTests(unittest.TestCase):
    def test_auto_selection_has_identical_intel_and_nvidia_call_shape(self) -> None:
        adapters = (FakeAdapter("nvidia"), FakeAdapter("intel"))
        processor = processor_api.GpuProcessor(adapters=adapters)
        for backend in ("nvidia", "intel"):
            source = FakeFrame(backend)
            image = processor.convert(source, format="rgb", layout="hwc", dtype="uint8")
            self.assertEqual(image.info.shape, (48, 64, 3))
            self.assertEqual(image.shape, (48, 64, 3))
            self.assertEqual(image.format, "rgb")
            self.assertEqual(image.pts_ns, 1234)
            self.assertEqual(image.info.copy_path, "gpu_copy")
            self.assertEqual(image.__dlpack_device__(), (2, 3))
            self.assertEqual(image.__dlpack__(stream=7), ("capsule", {"stream": 7}))
            with self.assertRaises(BufferError):
                image.__dlpack__(copy=True)
            image.close()
            image.close()
            self.assertEqual(source.retain_count, 1)
            self.assertEqual(source.close_count, 1)

    def test_backend_mismatch_and_missing_adapter_fail_without_cpu_fallback(self) -> None:
        source = FakeFrame("intel")
        with self.assertRaises(processor_api.GpuProcessingUnavailableError):
            processor_api.GpuProcessor(backend="nvidia", adapters=(FakeAdapter("nvidia"),)).convert(
                source
            )
        with self.assertRaises(processor_api.GpuProcessingUnavailableError):
            processor_api.GpuProcessor().convert(source)
        self.assertEqual(source.retain_count, 0)

    def test_request_and_adapter_output_are_strictly_validated(self) -> None:
        with self.assertRaises(ValueError):
            processor_api.GpuConversionRequest(format="nv12")
        adapter = FakeAdapter("nvidia")
        source = FakeFrame("nvidia")
        original = adapter.convert

        def invalid_output(frame: FakeFrame, request: object) -> object:
            image = original(frame, request)
            object.__setattr__(image.info, "copy_path", "zero_copy")
            return image

        adapter.convert = invalid_output
        with self.assertRaisesRegex(ValueError, "copy_path"):
            processor_api.GpuProcessor(adapters=(adapter,)).convert(source)
        self.assertEqual(source.close_count, 1)
        self.assertEqual(adapter.release_count, 1)

    def test_cleanup_failure_does_not_hide_conversion_failure(self) -> None:
        class FailingRetained:
            def close(self) -> None:
                raise RuntimeError("cleanup failure")

        source = FakeFrame("nvidia")
        source.retain = FailingRetained
        adapter = FakeAdapter("nvidia")

        def fail_conversion(frame: FakeFrame, request: object) -> object:
            raise ValueError("conversion failure")

        adapter.convert = fail_conversion
        with self.assertRaisesRegex(ValueError, "conversion failure"):
            processor_api.GpuProcessor(adapters=(adapter,)).convert(source)


if __name__ == "__main__":
    unittest.main()
