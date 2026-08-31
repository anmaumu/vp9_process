$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $Root

if (-not (Get-Command uv -ErrorAction SilentlyContinue)) {
    throw "uv が必要です。https://docs.astral.sh/uv/ からインストールしてください。"
}
New-Item -ItemType Directory -Force -Path models, results | Out-Null
if (-not (Test-Path "models\resnet50-v1-12.onnx")) {
    throw "モデルがありません。先に .\setup_windows.ps1 を実行してください。"
}

$Runs = @(
    @{ Version = "2024.6.0"; NumPy = "2.1.3" },
    @{ Version = "2025.2.0"; NumPy = "2.2.6" },
    @{ Version = "2025.4.1"; NumPy = "2.2.6" }
)
foreach ($Run in $Runs) {
    $Tag = $Run.Version.Replace(".", "_")
    $Venv = ".venv-win-$Tag"
    uv venv $Venv --python 3.12 --clear
    uv pip install --python "$Venv\Scripts\python.exe" "numpy==$($Run.NumPy)" "openvino==$($Run.Version)"
    & "$Venv\Scripts\python.exe" benchmark_openvino.py `
        --models models\resnet50-v1-12.onnx models\mobilenetv2-12.onnx `
        --output "results\windows_1core_ov_$Tag.csv" `
        --iterations 100 --warmup 20 --label "Windows-OV-$($Run.Version)" --single-core
}
