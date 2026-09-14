# OpenCV FFmpeg VP9デコーダーとの性能比較

## 結論

PythonからCPUでVP9をデコードし、所有権を持つ`uint8` BGR NumPy配列として受け取る
条件では、MKVCodecは1スレッド時にOpenCV FFmpegとほぼ同等だった。16スレッド時の
スループットはOpenCVが29.3%高かった一方、最初のフレームを受け取るまでの時間は
MKVCodecが28.83 ms（43.7%）短かった。

| decoder threads | MKVCodec median | OpenCV FFmpeg median | MKVCodec / OpenCV | MKVCodec first frame | OpenCV first frame |
|---:|---:|---:|---:|---:|---:|
| 1 | 68.42 fps | 70.00 fps | 97.75% | 44.45 ms | 40.64 ms |
| 16 | 101.34 fps | 131.02 fps | 77.34% | 37.08 ms | 65.91 ms |

これは1台の認定用PCにおける観測値であり、全環境に対する性能保証ではない。

## 比較方法

再現コードは
[`benchmarks/compare_opencv_vp9_decode.py`](https://github.com/anmaumu/vp9_process/blob/main/benchmarks/compare_opencv_vp9_decode.py)
に置いている。比較条件を次のように揃えた。

- 同一の10秒、600フレーム、1920x1080、60 fps VP9 WebMを使用
- 両経路ともhardware accelerationを無効化
- MKVCodecは`backend="cpu"`、`prefetch=0`、`conversion_threads=1`
- OpenCVは`CAP_FFMPEG`を明示し、`CAP_PROP_HW_ACCELERATION=VIDEO_ACCELERATION_NONE`
- codec thread数は両経路とも1または16とし、OpenCVの取得値が指定値と一致することを確認
- 両経路とも所有権を持つ連続した`uint8` BGR画像を返す
- ファイルopen、全フレームread、EOS到達、closeを計測区間に含める
- 1回warm-up後に5回計測し、実行順序を交互にして中央値を採用
- 全試行で600フレーム取得できなければ失敗

したがってこれはbitstream decodeだけではなく、Python APIからBGR NumPy配列を取得する
利用者視点のend-to-end測定である。

## 測定環境

| 項目 | 値 |
|---|---|
| 測定日 | 2026-09-14 |
| OS | Windows 11, AMD64 |
| CPU | Intel Xeon E5-2697 v2（12 core / 24 thread） |
| Python | 3.12.14 |
| MKVCodec | 0.1.0、Release build |
| OpenCV | 5.0.0 |
| OpenCV video backend | FFmpeg、prebuilt binaries |
| FFmpeg avcodec | 61.19.100 |
| NumPy | 2.5.3 |
| 入力SHA-256 | `d4ba2eef611ad18c4f61f5f246d1c614c8d4984b9f214bc071c8ea9c21ebe53a` |

## 詳細結果

| threads | decoder | fps中央値 | fps範囲 | 全600 frame中央値 | first-frame中央値 | first-frame p95 | CPU時間 / wall時間 |
|---:|---|---:|---:|---:|---:|---:|---:|
| 1 | MKVCodec | 68.42 | 67.74–68.55 | 8.769 s | 44.45 ms | 44.56 ms | 0.995 |
| 1 | OpenCV FFmpeg | 70.00 | 69.88–70.30 | 8.571 s | 40.64 ms | 40.92 ms | 0.995 |
| 16 | MKVCodec | 101.34 | 98.91–103.23 | 5.921 s | 37.08 ms | 38.63 ms | 1.618 |
| 16 | OpenCV FFmpeg | 131.02 | 130.36–134.98 | 4.579 s | 65.91 ms | 72.02 ms | 2.440 |

1スレッドではOpenCVが2.31%高速であり、実用上は同じ性能帯にある。16スレッドでは
OpenCVのスループットがMKVCodecの1.293倍だった。`CPU時間 / wall時間`はOpenCVが
2.440、MKVCodecが1.618であるため、OpenCVがCPU並列性をより多く利用したことが主因と
推測できる。ただし、この値だけから個々の内部処理時間を断定することはできない。

MKVCodecの16スレッドfirst-frame latencyが短い結果は、連続処理開始時の応答性には
有利である。一方、長い動画を最速で全件処理する用途では、現状のOpenCV FFmpeg経路が
有利だった。

## BGR出力の比較

性能測定とは別に、1スレッドで先頭、中央、末尾フレームのBGR画素を比較した。

| frame | mean absolute error | maximum absolute error | PSNR |
|---:|---:|---:|---:|
| 0 | 0.743 | 3 | 47.34 dB |
| 299 | 0.739 | 4 | 47.35 dB |
| 599 | 0.738 | 3 | 47.35 dB |

3フレーム平均の絶対誤差は0.740階調、最大差は4、平均PSNRは47.35 dBだった。
MKVCodecのlibyuv/Highway経路とFFmpegの色変換では8-bit丸め処理が異なるため、BGRの
SHA-256は一致しない。小さな丸め差であり、フレーム欠落や解像度・型の不一致はない。
この画素比較は性能測定区間の外で行い、速度を汚染しない。

## 再現方法

MKVCodecをRelease buildし、Python package、native libraryおよびOpenCVを利用可能にした
環境で実行する。

```shell
python -m pip install "opencv-python-headless==5.0.0.93"
python benchmarks/compare_opencv_vp9_decode.py \
  --input path/to/vp9-1080p60-600.webm \
  --threads 1 16 \
  --conversion-threads 1 \
  --warmup-runs 1 \
  --runs 5 \
  --output benchmark-results/opencv-vp9-comparison.json
```

スクリプトは環境情報、入力hash、個別試行、中央値、first-frame latency、process CPU時間、
代表フレームの画素差をversioned JSONへ保存する。画素確認だけを省略する場合は
`--skip-pixel-check`を指定できる。

## 解釈上の制約

- Windows上の1 CPU、1 build、1動画だけの測定であり、Linuxや別CPUへ一般化できない。
- OS cacheや電源・温度状態の影響を完全には排除していない。
- BGR変換を含むため、純粋なVP9 codec coreだけの比較ではない。
- GPU decode、GPU surface、prefetch、複数conversion workerは比較対象外である。
- OpenCVやFFmpegのversion、build optionが変われば結果も変わり得る。
- この観測値をrelease regression gateとして使う場合は、同じhardware/build classで別途
  baselineを承認する必要がある。

## 改善の示唆

1スレッド性能はOpenCVとほぼ同等に到達している。次のCPU最適化対象は、codec threadを
増やした際のdecode・BGR変換pipelineの並列利用率である。改善時もスループットだけでなく、
first-frame latency、CPU使用量、600フレーム完走、代表画素差を同時に確認する。
