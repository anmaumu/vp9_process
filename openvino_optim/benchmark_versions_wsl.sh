#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$root_dir"
command -v uv >/dev/null || { echo "uvが必要です: https://docs.astral.sh/uv/" >&2; exit 1; }
[[ -f models/resnet50-v1-12.onnx ]] || { echo "先に bash setup_wsl.sh を実行してください。" >&2; exit 1; }

versions=(2024.6.0 2025.2.0 2025.4.1)
numpy_versions=(2.1.3 2.2.6 2.2.6)
for index in "${!versions[@]}"; do
  version="${versions[$index]}"
  tag="${version//./_}"
  venv=".venv-wsl-$tag"
  uv venv "$venv" --python 3.12 --clear
  uv pip install --python "$venv/bin/python" \
    "numpy==${numpy_versions[$index]}" "openvino==$version"
  "$venv/bin/python" benchmark_openvino.py \
    --models models/resnet50-v1-12.onnx models/mobilenetv2-12.onnx \
    --output "results/wsl_1core_ov_$tag.csv" \
    --iterations 100 --warmup 20 --label "WSL2-OV-$version" --single-core
done
