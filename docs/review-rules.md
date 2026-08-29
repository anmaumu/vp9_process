# Specification Review Rules

参照repoのquality gateをMKVCodec向けに拡張したreview規則。

## External specification review

- 実装class/library名ではなく外部から観測可能な振る舞いになっているか。
- 各`EXT-*`に入力、結果、error、制約があるか。
- backend間で外部契約が異なる箇所が明示されているか。
- unsupportedとfallbackが区別されているか。
- 性能表現が測定可能か。未測定なら`PROPOSED/TBD`か。

## Internal specification review

- 各`INT-*`が少なくとも1つの`EXT-*`を実現しているか。
- ownership、thread、queue、GPU completion、cleanupが曖昧でないか。
- CPU/Intel/NVIDIAの差がinterfaceの外へ漏れすぎていないか。
- error pathでもresource invariantが成立するか。
- zero-copyの成立条件と降格規則が明示されているか。
- security、untrusted bitstream、dynamic loadが扱われているか。

## Acceptance / Test review

- ACが設計実装ではなく製品の完了条件になっているか。
- 各ACをTESTが実際に判定できるか。
- happy pathだけでなくboundary、failure、cancel、long-runを含むか。
- GPUなし環境のnegative testがあるか。
- performance claimにtraceまたはbaseline evidenceがあるか。
- package内容とlicense noticesを実artifactで検査するか。

## Change review

仕様変更PRは最低限次を含める。

1. 変更した`EXT/AC/INT/TEST` ID。
2. `traceability.md`更新。
3. `design-model.json`更新。
4. `legacy-coverage.md`への影響。
5. compatibility、ABI、licenseへの影響。
6. Human decisionが必要な`PROPOSED/TBD`一覧。


