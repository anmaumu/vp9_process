# Docgen specification

## Purpose

`docgen`は、MKVCodecの仕様・API・quality gateを検証し、レビュー可能なMarkdownと
配布可能な静的HTMLを自動生成する。

## Source of truth

次のファイルを入力の正本とする。

- `docs/external-spec/system-spec.md`
- `docs/internal-spec/system-design.md`
- `docs/test-spec/test-requirements.md`
- `docs/traceability.md`
- `docs/design-model.json`
- `docs/quality-gate.json`
- `docs/implementation-status.md`
- `include/mkvcodec/mkvc.h`
- `python/mkvcodec/_api.py`
- `LICENSE_POLICY.md`

`build/docgen-src`と`build/docsite`は生成物であり、直接編集しない。

## Commands

| Command | Behavior |
|---|---|
| `python tools/docgen.py check` | 入力検証と一時ディレクトリへの生成を行い、repositoryへ生成物を残さない |
| `python tools/docgen.py generate` | `build/docgen-src`へMarkdownを生成する |
| `python tools/docgen.py build` | Markdown生成後、MkDocs strict modeで`build/docsite`へHTMLを生成する |

`build`はDoxygenを必要とする。MkDocs siteは`build/docsite`、C/C++ HTMLとXMLは
`build/docsite/native`へ統合する。

## Validation gate

次の場合は終了code 1とする。

- 必須入力がない
- JSONをparseできない
- 正本文書から抽出したEXT/AC/INT/TEST件数とquality gate metricsが一致しない
- 外部仕様IDがdesign modelに存在しない
- EXT/AC/INT/TEST IDがtraceability文書に存在しない
- quality gateにfailureがある
- C ABI symbolを抽出できない
- 公開`MKVC_API` symbolにDoxygen commentがない
- Doxygenがない、またはsource commentの解析で警告・errorがある
- MkDocs strict buildが失敗する

## Determinism

生成内容へ日時、host名、絶対pathを含めない。同じcommitと同じdocgen versionからは
同じMarkdownを生成する。

## CI

pushおよびpull requestでcheck、unit test、HTML buildを実行する。HTMLは
`mkvcodec-documentation` artifactとして30日保存する。GitHub Pages公開は別の
明示的な公開判断後に有効化する。
