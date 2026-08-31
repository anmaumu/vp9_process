# OpenVINO: Windows と WSL2 の CPU 推論性能差 調査レポート

調査日: 2026-09-01
対象マシン: Intel Xeon E5-2697 v2 × 2、24物理コア / 48論理CPU、Windows 11 Pro 24H2 (build 26100)、WSL2 Ubuntu 24.04

## 結論

この実機で WSL2 が速い主因は、OSそのものの一般的な速度差ではなく、**OpenVINO が認識する CPU トポロジーと、それに基づく既定スレッド数の違い**だった。

- Windows は2ソケット × 12物理コアと認識する。OpenVINO の `LATENCY` 既定値は1ソケットを使うため、`INFERENCE_NUM_THREADS=12` となった。
- WSL2 は仮想CPUを1ソケット × 24物理コア相当（48論理CPU）として提示する。OpenVINO の既定値は `INFERENCE_NUM_THREADS=24` となった。
- その結果、既定値では WSL2 が ResNet-50 で約23%、MobileNetV2 で約20%短い平均レイテンシだった。
- Windows に `INFERENCE_NUM_THREADS=24` を明示すると、ResNet-50 は WSL2 と同等、MobileNetV2 は測定ばらつきの範囲で同等になった。
- Windows の CPU pinning をONにするだけでは改善しなかった。今回の決定要因は pinning 単独ではなく、12から24へのスレッド数増加である。

したがって、このマシンでまず採用すべき Windows 設定は次である。

```python
config = {
    "PERFORMANCE_HINT": "LATENCY",
    "INFERENCE_NUM_THREADS": 24,
    "ENABLE_CPU_PINNING": True,   # この実測では False とほぼ同等。併用負荷に応じて比較する
    "ENABLE_HYPER_THREADING": False,
}
compiled = ov.Core().compile_model(model, "CPU", config)
```

## 実測方法

Windows と WSL2 に別々の venv を作り、同じパッケージとモデルを使った。

- Python: Windows 3.12.11 / WSL2 3.12.3
- OpenVINO: 2025.2.0（同一ビルド）
- NumPy: 2.2.6
- モデル: ONNX Model Zoo の ResNet-50 v1-12、MobileNetV2-12
- 入力: batch 1、FP32、疑似乱数固定、CPUデバイス
- 各条件: warm-up 6回後、同期推論30回
- 2モデルは、重い標準CNNと軽量CNNで傾向が共通するかを見るために選定

これはアプリケーション側の前処理・画像読込・後処理を除いた OpenVINO Runtime の推論時間である。Windows と WSL2 は順番に測定し、同時実行によるCPU競合を避けた。

## 結果

代表値（30回の平均、batch 1、低いほど良い）:

| 環境 / 設定 | 実効スレッド | pinning | ResNet-50 | MobileNetV2 |
|---|---:|---:|---:|---:|
| Windows 既定 | 12 | OFF | 52.106 ms | 6.232 ms |
| WSL2 既定 | 24 | ON | 40.293 ms | 5.015 ms |
| Windows, 24 threads, pin ON | 24 | ON | **39.597 ms** | 5.364 ms |
| Windows, 24 threads, pin OFF | 24 | OFF | **39.268 ms** | 5.470 ms |
| Windows, 12 threads, pin ON | 12 | ON | 57.760 ms | 6.117 ms |

比較:

- WSL2既定 / Windows既定: ResNet-50 は 22.7%短縮、MobileNetV2 は 19.5%短縮。
- Windows 24 threads (pin ON) / Windows既定: ResNet-50 は 24.0%短縮、MobileNetV2 は 13.9%短縮。
- Windows 24 threads (pin ON) と WSL2既定: ResNet-50 は Windows が1.7%速く、MobileNetV2 は WSL2が6.5%速い。この程度の軽量モデルの差は、30回の単回試験ではOS差と断定できない。

実効プロパティを問い合わせた結果も原因を直接裏付けた。

| 環境 | `INFERENCE_NUM_THREADS` | `NUM_STREAMS` | `ENABLE_CPU_PINNING` | `ENABLE_HYPER_THREADING` |
|---|---:|---:|---:|---:|
| Windows既定 | 12 | 1 | False | False |
| WSL2既定 | 24 | 1 | True | False |

生データは `results/windows.csv` と `results/wsl.csv` に保存している。

## なぜこの差が発生するか

OpenVINO の `LATENCY` ヒントは既定で1ストリーム、1ソケットを使う。公式資料でも、Windows/Linuxともにレイテンシーモードの推論スレッド数は「1ソケット上の対象コア数」であり、CPU pinning は Windows で既定OFF、Linuxで既定ONとされている。

物理WindowsからはCPUが2ソケットに見える一方、今回の WSL2 の `lscpu` は `Socket(s): 1`, `Core(s) per socket: 24`, `Thread(s) per core: 2`, `NUMA node(s): 1` と報告した。OpenVINO はこの提示トポロジーに従い、同じ `LATENCY` でも前者を12スレッド、後者を24スレッドにしたと判断できる。

CPU pinning の既定差は一般には、スレッド移動、キャッシュ再利用、NUMAのリモートメモリアクセスを通じて差を生み得る。ただし本実測では、Windows 24スレッドで pin ON/OFF の差が小さく、pinningだけをONにした12スレッド条件は改善しなかった。従って、今回 pinning は副次要因であり、主因ではない。

## 追加調査: 1スレッド・1コア固定

両環境で `INFERENCE_NUM_THREADS=1`、`NUM_STREAMS=1`、HT無効、CPU pinning有効とし、さらにプロセスの実行可能CPUを論理CPU 0だけに制限した。Windowsでは `SetProcessAffinityMask(mask=1)`、WSL2では `sched_setaffinity({0})` を使用した。warm-up 20回後、200回を測定した。

| 環境 | モデル | 平均 | 中央値 | p90 | FPS |
|---|---|---:|---:|---:|---:|
| Windows / 1 thread / CPU 0 | ResNet-50 | **358.280 ms** | **358.010 ms** | **358.880 ms** | **2.791** |
| WSL2 / 1 thread / CPU 0 | ResNet-50 | 379.365 ms | 378.839 ms | 383.527 ms | 2.636 |
| Windows / 1 thread / CPU 0 | MobileNetV2 | **30.336 ms** | **30.343 ms** | **30.523 ms** | **32.958** |
| WSL2 / 1 thread / CPU 0 | MobileNetV2 | 32.882 ms | 32.362 ms | 33.385 ms | 30.404 |

平均レイテンシでは、WindowsがWSL2より ResNet-50で5.6%、MobileNetV2で7.7%短かった。プロセスCPU時間 / wall time は Windowsで約1.00、WSL2で約0.97となり、どちらもほぼ1 CPUを使用していることも確認した。

この結果から、**同じ1スレッド条件でWSL2の演算自体が速いわけではない**。むしろ本条件ではWindowsネイティブがわずかに速い。最初のWSL2優位は、WSL2上でOpenVINOが既定24スレッド、Windows上で既定12スレッドを選んだことによる並列度差で説明できる。

注意点として、WindowsのCPU 0はホストの論理プロセッサへの直接的なアフィニティである。一方、WSL2のCPU 0はゲストから見た仮想CPU 0で、ホスト物理コアへの最終割当はHyper-Vが管理する。そのため「同一の物理コアを両OSから直接指定した比較」ではなく、「各OSから見えるCPU 0を1個だけ許可した比較」である。それでも、OpenVINOが利用できる並列度を1に揃える目的は満たしている。

追加の生データは `results/windows_1thread_cpu0.csv` と `results/wsl_1thread_cpu0.csv` に保存した。再実行には既存コマンドへ `--single-core --iterations 200 --warmup 20` を追加する。

## OpenVINOバージョン別: 1スレッド・1コア固定

OpenVINO 2024.6.0、2025.2.0、2025.4.1をそれぞれ独立venvへ導入し、両OSでCPU 0のみ、`INFERENCE_NUM_THREADS=1`、`NUM_STREAMS=1`、warm-up 20回後に100回測定した。入力、モデル、測定プログラムは同一である。

| OpenVINO | Windows ResNet-50 | WSL2 ResNet-50 | Windows MobileNetV2 | WSL2 MobileNetV2 |
|---|---:|---:|---:|---:|
| 2024.6.0 | 360.033 ms | 379.809 ms | 30.478 ms | 32.839 ms |
| 2025.2.0 | **358.375 ms** | 379.754 ms | **30.440 ms** | 33.008 ms |
| 2025.4.1 | 358.793 ms | **379.396 ms** | 30.473 ms | **32.848 ms** |

同一OS内の最大差は、WindowsのResNet-50で約0.5%、MobileNetV2で約0.1%、WSL2のResNet-50で約0.1%、MobileNetV2で約0.5%だった。測定ばらつきと比べても小さく、このCPU・2モデル・FP32・1スレッド条件では、バージョン更新による一貫した性能向上または低下は確認できない。

一方、Windowsは全バージョンでWSL2より速く、ResNet-50で約5.2～5.6%、MobileNetV2で約7.2～7.8%短い。したがって、1コア時のOS差は特定OpenVINO版に固有の回帰ではない。

2024.6.0は依存制約のためNumPy 2.1.3、2025系はNumPy 2.2.6を使用した。NumPyによる入力生成は計測区間外で、OpenVINOへ渡す配列はいずれも同じshape・FP32である。2024.6.0のWindows CPUプラグインは要求した `ENABLE_CPU_PINNING=True` を実効値 `False` と報告したが、測定プログラム自身がプロセス全体をWindows APIでCPU 0へ固定しているため、1 CPU制限は維持される。

OpenVINO公式リリースノートによると、2026.0以降のCPUプラグインはAVX2が最低要件である。今回のXeon E5-2697 v2はAVXまででAVX2非対応のため、2026系CPU推論は比較対象外とした。

生データは次の6ファイルに保存した。

- `results/windows_1core_ov_2024_6_0.csv`
- `results/windows_1core_ov_2025_2_0.csv`
- `results/windows_1core_ov_2025_4_1.csv`
- `results/wsl_1core_ov_2024_6_0.csv`
- `results/wsl_1core_ov_2025_2_0.csv`
- `results/wsl_1core_ov_2025_4_1.csv`

## Windows 側の推奨設定

### 1. 低レイテンシ（同期・batch 1）

まず `LATENCY + INFERENCE_NUM_THREADS=24 + HT OFF` を採用する。pinning は単独アプリならONを第一候補にし、動画デコードなど他の重い処理と同居する場合はON/OFFを実アプリ全体で比較する。

```python
import openvino as ov

core = ov.Core()
model = core.read_model("model.onnx")
compiled = core.compile_model(model, "CPU", {
    "PERFORMANCE_HINT": "LATENCY",
    "INFERENCE_NUM_THREADS": 24,
    "ENABLE_HYPER_THREADING": False,
    "ENABLE_CPU_PINNING": True,
})
print(compiled.get_property("INFERENCE_NUM_THREADS"))
```

この24は普遍的な推奨値ではなく、このマシンの物理コア総数に対応する。別マシンでは12、16、全物理コア数などを測定して決める。

### 2. 複数リクエストの総スループット

単発レイテンシではなく、多数リクエスト/複数カメラの総処理量が目的なら `PERFORMANCE_HINT=THROUGHPUT` と非同期推論を使う。`NUM_STREAMS` を手動指定する前に高レベルヒントを使い、`compiled.get_property("OPTIMAL_NUMBER_OF_INFER_REQUESTS")` の数を並列要求数の出発点にする。同期 `infer()` を1本だけ呼ぶ試験では、スループットモードの利点は評価できない。

### 3. OSと計測条件

- 現在のWindows電源プランは「バランス」。再現性と最大性能を優先する本番機では、高パフォーマンス相当を候補にし、変更前後を測定する。ただし消費電力・発熱が増える。
- 初回コンパイルを計測に含めず、warm-up後を測る。
- OpenVINO、モデル精度、batch、前後処理、同期/非同期、ストリーム数を両OSで一致させる。
- 実際のアプリがVP9デコードも行うなら、推論単体だけでなくデコード込みで再測定する。CPU pinningがデコーダのスレッドを圧迫する可能性がある。
- 24スレッド設定は2物理ソケットをまたぐ。モデルやメモリ配置によってはNUMAコストが増えるため、12/24の両方をモデルごとに測る。本機の2モデルでは24が勝った。

## 再実行方法

Windows PowerShell:

```powershell
.\.venv-win\Scripts\python.exe benchmark_openvino.py `
  --models models\resnet50-v1-12.onnx models\mobilenetv2-12.onnx `
  --output results\windows.csv --iterations 30 --warmup 6 --label Windows
```

WSL2:

```bash
cd /mnt/f/vp9_process/openvino_optim
.venv-wsl/bin/python benchmark_openvino.py \
  --models models/resnet50-v1-12.onnx models/mobilenetv2-12.onnx \
  --output results/wsl.csv --iterations 30 --warmup 6 --label WSL2
```

より確かな差を出すには、バックグラウンド負荷を止め、各条件を100回以上・複数ラウンド・順番を入れ替えて測定し、中央値とp90を比較する。

## 参考資料

- [OpenVINO: Performance Hints and Thread Scheduling](https://docs.openvino.ai/nightly/openvino-workflow/running-inference/inference-devices-and-modes/cpu-device/performance-hint-and-thread-scheduling.html)
- [OpenVINO: High-level Performance Hints](https://docs.openvino.ai/2024/openvino-workflow/running-inference/optimize-inference/high-level-performance-hints.html)
- [OpenVINO: CPU Device and supported properties](https://docs.openvino.ai/2024/openvino-workflow/running-inference/inference-devices-and-modes/cpu-device.html)
- [OpenVINO: Query Device Properties](https://docs.openvino.ai/2024/openvino-workflow/running-inference/inference-devices-and-modes/query-device-properties.html)

## 制約

本調査はCPUデバイス、batch 1、FP32、同期推論を対象とする。RTX 2060は OpenVINO のIntel GPUプラグイン対象ではないため使用していない。また、WSL2が報告する1ソケット/1 NUMAノードは仮想トポロジーであり、物理NUMAが消滅したことを意味しない。モデル変換精度や認識精度の検証ではなく、同一モデルの実行性能比較である。
