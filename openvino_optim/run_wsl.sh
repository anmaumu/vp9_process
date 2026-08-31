#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$root_dir"
models=(models/resnet50-v1-12.onnx models/mobilenetv2-12.onnx)

.venv-wsl/bin/python benchmark_openvino.py --models "${models[@]}" \
  --output results/wsl.local.csv --iterations 30 --warmup 6 --label WSL2
.venv-wsl/bin/python benchmark_openvino.py --models "${models[@]}" \
  --output results/wsl_1core.local.csv --iterations 200 --warmup 20 \
  --label WSL2-1Core --single-core
