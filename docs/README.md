# MKVCodec 設計仕様

このディレクトリを、MKVCodecの外部仕様・内部仕様・受け入れ条件・テスト要求の正本とする。

## 文書構成

| 文書 | 役割 |
|---|---|
| `external-spec/system-spec.md` | 利用者・外部システムから観測可能な振る舞い |
| `internal-spec/system-design.md` | 外部仕様を実現するArchitecture、処理、制約 |
| `test-spec/test-requirements.md` | TEST ID単位の検証条件と実行環境 |
| `traceability.md` | `EXT → AC → INT → TEST` の追跡表 |
| `design-model.json` | 同じ追跡情報の機械可読な正規化表現 |
| `quality-gate.json` | 仕様品質ゲートの結果 |
| `legacy-coverage.md` | 旧仕様の全節がどこへ移行したかの監査表 |
| `../LICENSE_POLICY.md` | ライセンス・特許・商標・再配布方針 |

`SPECIFICATION.md` はDraft 0.1時点の移行元snapshotとして保持する。内容を削除せず、`legacy-coverage.md`で新仕様への移行先を追跡する。

## ID規約

| Prefix | 意味 | 例 |
|---|---|---|
| `EXT-*` | 外部要求 | `EXT-ENC-001` |
| `AC-*` | 受け入れ条件 | `AC-ENC-001` |
| `INT-*` | 内部設計規則 | `INT-PIPE-001` |
| `TEST-*` | テスト要求 | `TEST-ENC-001` |
| `ADR-*` | 設計判断 | `ADR-CODEC-001` |

Domain code:

- `SYS`: 対象環境・全体制約
- `CONT`: container
- `CODEC`: codec
- `DEC`: decode
- `ENC`: encode
- `BACK`: backend/device selection
- `FRAME`: frame/memory interop
- `PIPE`: concurrency/async/resource lifetime
- `ABI`: C ABI
- `PY`: Python
- `CS`: C#
- `ERR`: error/cleanup
- `OBS`: statistics/observability
- `PERF`: performance
- `PKG`: build/package/compliance

## Status

- `CONFIRMED`: Human合意済み
- `PROPOSED`: 設計提案。Human確認待ち
- `TBD`: 未決定
- `NOT_APPLICABLE`: 適用対象外
- `CONFLICTING`: 情報が競合しており解決が必要

各節にStatusとSourcesを持たせる。推測した内容を`CONFIRMED`にしてはならない。

## Quality gate

次をすべて満たす場合のみ仕様レビューをPASSとする。

1. 必須節が存在する。
2. `CONFLICTING`がない。
3. blockingな`TBD`がない。
4. 全`EXT-*`に少なくとも1つの`AC-*`がある。
5. 全`EXT-*`に少なくとも1つの`INT-*`がある。
6. 全`INT-*`に少なくとも1つの`TEST-*`がある。
7. 全`AC-*`に少なくとも1つの`TEST-*`がある。
8. H.264/HEVCが公開codecまたは暗黙fallbackに含まれない。
9. GPUなし環境でもCPU Coreをloadできる仕様になっている。
10. license/compliance gateが配布仕様とtraceされている。


