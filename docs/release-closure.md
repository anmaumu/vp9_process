# v0.1 release closure

この文書を、MKVCodec v0.1の**必要な残件の正本**とする。
`implementation-plan.md`の未完了行は詳細backlogであり、v0.1の完成条件を追加しない。
新しい作業をv0.1必須へ追加できるのは、既存外部仕様への不適合、data loss、security、
ABI破壊、またはlicense違反を防ぐ場合だけとする。

## 完成の定義

次の`RC-01..07`がすべて`DONE`または明示的な`WAIVED`になり、mainのDocumentation、
sanitizer/fuzz、対象platform testが成功すればv0.1を完成とする。対応実機がないbackendは、
無期限にreleaseを止めず、公開対応表から外して`preview/unqualified`と表示する選択を認める。

## 必須対応

| ID | 状態 | 必要な対応 | 完了条件 |
|---|---|---|---|
| `RC-01` | `DONE` | CPU入力layout契約 | plane数、dtype、寸法、stride、必要alignmentをC ABI/Python/.NETで一貫して検証する。strict modeは不一致を拒否し、copy許可時だけ明示的に正規化する。parameterized testが成功する |
| `RC-02` | `DONE` | copy経路の説明可能性 | GPU→NumPy download、色変換、CPU正規化をedge別metricsへ記録する。`require_gpu_resident`時のCPU copy拒否を回帰試験し、driver内部copyは「未観測」と区別する |
| `RC-03` | `DONE` | Intel Linux USMの公開判定 | Arc B580でcross-device拒否、同一native context許可、producer event、fault伝播、capacity 1 pool backpressure、30分soakを通した。core単独ではopaque USM provenanceを完全検証できないため、v0.1は`api_stability="preview"`に固定する |
| `RC-04` | `WAIVED_PREVIEW` | Intel Windows D3D11認定 | Intel Windows実機がないためv0.1の認定対象外とし、D3D11契約・実装はpreview/unqualifiedとして保持する。stable対応表への追加にはdecode→external processing→encodeのC++/Python/.NET実機試験が必要 |
| `RC-05` | `WAIVED_PREVIEW` | NVIDIA AV1認定 | 現RTX 2060はAV1 encode非対応のためv0.1の認定対象外とし、NVENC AV1実装はpreview/unqualifiedとして保持する。stable対応表への追加にはAV1対応NVIDIA実機でのNVDEC→外部CUDA処理→NVENC試験が必要 |
| `RC-06` | `DONE` | 性能基準の固定 | Windows CPU VP9とLinux Arc B580 Intel VP9 decode→AV1 encodeについて、3回中央値の1080p throughput/latency/peak RSS baseline、入力hash、環境、15%の既定回帰率と個別閾値を保存し、fail-closed gateを実測候補へ適用した |
| `RC-07` | `WAITING_DECISION` | release governance | project LICENSEを決定し、legal review後にwheel/NuGetのnative/legal/SBOM実artifact gateを通して公開する。vendor driver/runtimeは同梱しない |

## v0.1完成条件から外す項目

以下はbacklogとして保持するが、v0.1を止めない。

- 10-bit/P010公開APIとSSIM認定
- timestamp seek
- GPU resize/crop/color conversion等の内蔵画像処理
- NVIDIA decoded CUarray exportの追加形態（既存linear CUDA pointer/DLPackは維持）
- 全解像度、全GPU世代、4Kの性能・耐久matrix
- vendor profilerによる全driver内部copyの完全証明
- 実driver reset/device removal試験
- shared-sessionの追加最適化
- macOS、H.264、HEVC

## 実施順序

1. `RC-01` CPU layout validation
2. `RC-02` copy-edge metrics
3. `RC-03` Intel Linux USM qualification
4. `RC-06` release対象backendの1080p baseline
5. `RC-04`と`RC-05`を実機認定、またはpreviewへ明示的に縮退
6. `RC-07` LICENSE、artifact、NuGet/wheel公開

リファクタ、追加最適化、対応format追加は、上記項目を直接閉じる場合を除いてrelease後へ送る。
