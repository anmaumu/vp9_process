# OpenVINO Windows / WSL2 CPU benchmark

同一マシン上のWindowsネイティブとWSL2で、OpenVINO CPU推論を比較・再現するための測定一式です。

調査対象機（Xeon E5-2697 v2 ×2）では、OpenVINOの既定値がWindowsで12スレッド、WSL2で24スレッドとなり、WSL2のほうが約20%高速でした。Windowsでも `INFERENCE_NUM_THREADS=24` を明示するとWSL2相当になりました。一方、1スレッド・CPU 0固定ではWindowsが5～8%高速でした。詳細は [REPORT.md](REPORT.md) を参照してください。

## 含まれるもの

- `benchmark_openvino.py`: 共通ベンチマーク本体
- `setup_windows.ps1`: Windows venv作成、依存関係・モデル取得
- `setup_wsl.sh`: WSL2 venv作成、依存関係・モデル取得
- `run_windows.ps1`: Windowsの標準測定と1コア測定
- `run_wsl.sh`: WSL2の標準測定と1コア測定
- `benchmark_versions_windows.ps1`: WindowsでOpenVINO 3バージョンを比較
- `benchmark_versions_wsl.sh`: WSL2でOpenVINO 3バージョンを比較
- `results/*.csv`: 調査時の生データ
- `REPORT.md`: 条件、結果、原因分析、推奨設定

ONNXモデルとvenvはサイズが大きいためGitには含めず、セットアップスクリプトで取得します。

## Windowsで実行

PowerShellでこのディレクトリへ移動し、次を実行します。Pythonが未導入でも、`uv` が利用可能ならPython 3.12を自動取得できます。

```powershell
.\setup_windows.ps1
.\run_windows.ps1
```

標準測定だけを直接実行する場合:

```powershell
.\.venv-win\Scripts\python.exe benchmark_openvino.py `
  --models models\resnet50-v1-12.onnx models\mobilenetv2-12.onnx `
  --output results\windows.local.csv --iterations 30 --warmup 6 --label Windows
```

## WSL2で実行

```bash
cd /mnt/f/vp9_process/openvino_optim
bash setup_wsl.sh
bash run_wsl.sh
```

## 1 core / 1 thread測定

`--single-core` はOpenVINOを1スレッドにし、プロセス全体を論理CPU 0だけに制限します。測定プログラム内でWindowsは `SetProcessAffinityMask`、Linux/WSL2は `sched_setaffinity` を呼びます。

```powershell
.\.venv-win\Scripts\python.exe benchmark_openvino.py `
  --models models\resnet50-v1-12.onnx models\mobilenetv2-12.onnx `
  --output results\windows_1core.local.csv --iterations 200 --warmup 20 `
  --label Windows-1Core --single-core
```

```bash
.venv-wsl/bin/python benchmark_openvino.py \
  --models models/resnet50-v1-12.onnx models/mobilenetv2-12.onnx \
  --output results/wsl_1core.local.csv --iterations 200 --warmup 20 \
  --label WSL2-1Core --single-core
```

外部からOSアフィニティも指定する場合、Windowsは `/affinity 1`、WSL2は `taskset -c 0` を使えます。`1` はCPU番号ではなく、CPU 0だけを許可するビットマスクです。

```powershell
cmd.exe /c start "" /wait /affinity 1 .\.venv-win\Scripts\python.exe your_script.py
```

```bash
taskset -c 0 .venv-wsl/bin/python your_script.py
```

WSL2のCPU 0は仮想CPUです。ホスト物理コアへの最終配置はHyper-Vが管理するため、これは同一物理コアの直接比較ではなく、各環境で利用可能な論理CPUを1個に揃える比較です。

## OpenVINOバージョン比較

2024.6.0、2025.2.0、2025.4.1を独立venvへ導入し、1 core / 1 threadで各100回測定します。

```powershell
.\benchmark_versions_windows.ps1
```

```bash
bash benchmark_versions_wsl.sh
```

OpenVINO 2026.0以降はCPUプラグインがAVX2必須です。AVX2非対応CPUでは2025.4.1以前を使用してください。

## Windowsアプリ向け推奨値

今回の24物理コア機で、batch 1の同期推論を短くする設定です。別のCPUでは物理コア数に合わせ、12/全物理コア数などを実測してください。

```python
config = {
    "PERFORMANCE_HINT": "LATENCY",
    "INFERENCE_NUM_THREADS": 24,
    "ENABLE_CPU_PINNING": True,
    "ENABLE_HYPER_THREADING": False,
}
compiled = ov.Core().compile_model(model, "CPU", config)
```

## 測定上の注意

- 初回コンパイルを除外し、warm-up後を測定します。
- OpenVINO版、モデル、batch、精度、同期/非同期を揃えてください。
- バックグラウンド負荷を止め、複数ラウンド測定してください。
- VP9デコードを伴う実アプリでは、推論単体に加えてデコード・前後処理込みでも比較してください。
- `PERFORMANCE_HINT=THROUGHPUT` は複数の非同期要求で評価します。本スクリプトは単発レイテンシ中心です。
