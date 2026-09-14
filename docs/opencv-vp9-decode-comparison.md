# OpenCV FFmpeg VP9デコーダーとの性能比較

## 結論

PythonからCPUでVP9をデコードし、所有権を持つ`uint8` BGR NumPy配列として受け取る
条件では、MKVCodecの既定の自動BGR変換を使うと、1 decoder threadではOpenCV FFmpeg
より15.9%高速、16 decoder threadsでは差0.04%で実質同等だった。以前の比較で
MKVCodecが遅かった主因は、MKVCodecだけBGR変換を1 threadへ制限していたことである。

| decoder threads | MKVCodec median | OpenCV FFmpeg median | MKVCodec / OpenCV | MKVCodec first frame | OpenCV first frame |
|---:|---:|---:|---:|---:|---:|
| 1 | 81.69 fps | 70.49 fps | 115.88% | 41.41 ms | 40.46 ms |
| 16 | 131.56 fps | 131.61 fps | 99.97% | 36.80 ms | 74.97 ms |

これは1台の認定用PCにおける観測値であり、全環境に対する性能保証ではない。

修正後の完全な既定値同士では、MKVCodecの`threads=0`は16 decoder threadsへ解決され、
`prefetch=4`と自動BGR変換を使用する。5回の中央値はMKVCodec 200.15 fps、OpenCV
FFmpeg 131.96 fpsで、MKVCodecが51.7%高速だった。この既定値比較は利用者の操作感を
比較するもので、prefetchを無効化した上表とは目的が異なる。

## 比較方法

再現コードは
[`benchmarks/compare_opencv_vp9_decode.py`](https://github.com/anmaumu/vp9_process/blob/main/benchmarks/compare_opencv_vp9_decode.py)
に置いている。比較条件を次のように揃えた。

- 同一の10秒、600フレーム、1920x1080、60 fps VP9 WebMを使用
- 両経路ともhardware accelerationを無効化
- MKVCodecは`backend="cpu"`、`prefetch=0`、`conversion_threads=0`。この環境では自動的に
  callerを含む4 conversion threadsを使用
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
| CPU | Intel Xeon E5-2697 v2 x2（合計24 core / 48 logical processors） |
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
| 1 | MKVCodec | 81.69 | 80.99–81.90 | 7.345 s | 41.41 ms | 41.66 ms | 1.269 |
| 1 | OpenCV FFmpeg | 70.49 | 70.11–71.08 | 8.512 s | 40.46 ms | 40.85 ms | 0.994 |
| 16 | MKVCodec | 131.56 | 128.31–142.87 | 4.561 s | 36.80 ms | 41.65 ms | 2.255 |
| 16 | OpenCV FFmpeg | 131.61 | 130.28–132.49 | 4.559 s | 74.97 ms | 77.33 ms | 2.392 |

1 decoder threadではMKVCodecがBGR変換を並列化するため、OpenCVより15.9%高速だが
CPU使用量も多い。16 decoder threadsではスループットが実質同じで、MKVCodecの
`CPU時間 / wall時間`はOpenCVより約5.7%低かった。

MKVCodecの16スレッドfirst-frame latencyはOpenCVより38.17 ms短かった。この結果では
連続処理のスループットを維持しながら、処理開始時の応答性にも優位性がある。

### 単一conversion threadに制限した診断結果

`conversion_threads=1`では、1 decoder thread時に68.42対70.00 fpsでほぼ同等、
16 decoder threads時に101.34対131.02 fpsとなった。このときのCPU時間/wall時間は
MKVCodec 1.62、OpenCV 2.44であり、MKVCodecのBGR変換だけを直列化したことでCPUを
十分利用できていなかった。この診断条件を通常利用時の代表性能として扱ってはならない。

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
  --conversion-threads 0 \
  --warmup-runs 1 \
  --runs 5 \
  --output benchmark-results/opencv-vp9-comparison.json
```

スクリプトは環境情報、入力hash、個別試行、中央値、first-frame latency、process CPU時間、
代表フレームの画素差をversioned JSONへ保存する。画素確認だけを省略する場合は
`--skip-pixel-check`を指定できる。`--threads 0`では両ライブラリの自動codec thread設定を
比較し、OpenCVが報告した実thread数もJSONへ記録する。

## 解釈上の制約

- Windows上の1 CPU、1 build、1動画だけの測定であり、Linuxや別CPUへ一般化できない。
- OS cacheや電源・温度状態の影響を完全には排除していない。
- BGR変換を含むため、純粋なVP9 codec coreだけの比較ではない。
- GPU decode、GPU surface、prefetchは比較対象外である。
- OpenCVやFFmpegのversion、build optionが変われば結果も変わり得る。
- この観測値をrelease regression gateとして使う場合は、同じhardware/build classで別途
  baselineを承認する必要がある。

## 改善の示唆

通常のBGR取得性能はOpenCVと同等以上に到達している。次のCPU最適化対象はlibvpxの
external frame bufferを利用し、libvpx出力から所有I420 frameへの中間コピーを除去する
ことである。改善時もスループットだけでなく、first-frame latency、CPU使用量、frame
leaseの寿命、600フレーム完走、代表画素差を同時に確認する。
