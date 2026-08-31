#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$root_dir"
mkdir -p models results
python3 -m venv .venv-wsl
.venv-wsl/bin/python -m pip install -r requirements.txt

if [[ ! -f models/resnet50-v1-12.onnx ]]; then
  curl -L --fail --output models/resnet50-v1-12.onnx \
    https://github.com/onnx/models/raw/main/validated/vision/classification/resnet/model/resnet50-v1-12.onnx
fi
if [[ ! -f models/mobilenetv2-12.onnx ]]; then
  curl -L --fail --output models/mobilenetv2-12.onnx \
    https://github.com/onnx/models/raw/main/validated/vision/classification/mobilenet/model/mobilenetv2-12.onnx
fi

echo "WSL2環境の準備が完了しました。"
