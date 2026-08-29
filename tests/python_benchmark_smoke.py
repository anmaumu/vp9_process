import argparse
import importlib.util
import json
import tempfile
from pathlib import Path


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    module_path = root / "benchmarks" / "pipeline_benchmark.py"
    spec = importlib.util.spec_from_file_location("pipeline_benchmark", module_path)
    assert spec is not None and spec.loader is not None
    benchmark = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(benchmark)

    with tempfile.TemporaryDirectory() as directory:
        output = Path(directory) / "result.json"
        arguments = argparse.Namespace(
            backend="cpu", codec="vp9", width=320, height=240,
            frames=12, fps=30, quality=32, queue_size=2, prefetch=2,
            output=str(output), media_output=None,
        )
        result = benchmark.run(arguments)
        output.write_text(json.dumps(result), encoding="utf-8")
        loaded = json.loads(output.read_text(encoding="utf-8"))
        assert loaded["schema_version"] == 1
        assert loaded["case"]["frames"] == 12
        assert loaded["measurements"]["encode_fps"] > 0
        assert loaded["measurements"]["decode_fps"] > 0
        assert loaded["measurements"]["encoded_bytes"] > 0
        assert loaded["observed_path"]["zero_copy"] is False


if __name__ == "__main__":
    main()
