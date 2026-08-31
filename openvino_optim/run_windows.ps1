$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $Root
$Python = ".\.venv-win\Scripts\python.exe"
$Models = @("models\resnet50-v1-12.onnx", "models\mobilenetv2-12.onnx")

& $Python benchmark_openvino.py --models $Models `
    --output results\windows.local.csv --iterations 30 --warmup 6 --label Windows
& $Python benchmark_openvino.py --models $Models `
    --output results\windows_1core.local.csv --iterations 200 --warmup 20 `
    --label Windows-1Core --single-core
