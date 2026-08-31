$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $Root

if (-not (Get-Command uv -ErrorAction SilentlyContinue)) {
    throw "uv が必要です。https://docs.astral.sh/uv/ からインストールしてください。"
}

New-Item -ItemType Directory -Force -Path models, results | Out-Null
uv venv .venv-win --python 3.12
uv pip install --python .venv-win\Scripts\python.exe -r requirements.txt

$Models = @{
    "models\resnet50-v1-12.onnx" = "https://github.com/onnx/models/raw/main/validated/vision/classification/resnet/model/resnet50-v1-12.onnx"
    "models\mobilenetv2-12.onnx" = "https://github.com/onnx/models/raw/main/validated/vision/classification/mobilenet/model/mobilenetv2-12.onnx"
}
foreach ($Entry in $Models.GetEnumerator()) {
    if (-not (Test-Path $Entry.Key)) {
        Invoke-WebRequest -Uri $Entry.Value -OutFile $Entry.Key
    }
}

Write-Host "Windows環境の準備が完了しました。"
