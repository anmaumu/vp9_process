# Legacy Specification Coverage

旧`SPECIFICATION.md`と`LICENSE_POLICY.md`の内容を新フォーマットへ移行した監査表。旧文書は移行完了確認まで削除しない。

## SPECIFICATION.md

| Legacy section | New canonical location | Coverage |
|---|---|---|
| 1. 目的 | external 1 | Complete |
| 2. Scope / Out of Scope | external 2 | Complete |
| 3. 基本方針 | external 5/9、internal 2/7/8/12/14 | Complete |
| 4. 対応マトリクス | external 5.4、internal 9 | Complete |
| 5. システム構成 | internal 2/3 | Complete |
| 6. Python外部仕様 | external 5/6/7/8 | Complete |
| 7. C ABI/C# | external 6.3/6.4、internal 12 | Complete |
| 8.1 Data model | internal 4 | Complete |
| 8.2 Common interfaces | internal 3 | Complete |
| 8.3 Container | internal 9.1 | Complete |
| 8.4 CPU backend | internal 9.2 | Complete |
| 8.5 Intel backend | internal 9.3 | Complete |
| 8.6 NVIDIA backend | internal 9.4 | Complete |
| 8.7 Async pipeline | internal 5/6/8 | Complete |
| 8.8 Mode defaults | external 5.6、internal 8 | Complete |
| 8.9 Memory ownership | external 5.5、internal 4/8 | Complete |
| 8.10 Statistics | external 8、internal 10 | Complete |
| 9. Build/distribution | external 9、internal 2/9/12、LICENSE_POLICY | Complete |
| 10. Acceptance criteria | external 10 | Complete; ID normalized |
| 11. Tests | test-spec全体 | Complete; ID normalized |
| 12. CI | test-spec 4、docs/README quality gate | Complete |
| 13. Implementation phases | この表下のPhase plan | Complete |
| 14. Existing implementation | この表下のExisting assets | Complete |
| 15.1 Names | external 9、LICENSE_POLICY 3.4/9 | Complete |
| 15.2 CPU AV1 | external 5.4、internal 9.2 | Complete |
| 15.3 Rate control | external 5.6 | Complete |
| 15.4 WebM/MKV difference | external 5.1、internal 9.1 | Complete |
| 15.5 NumPy ownership | external 5.3/5.5、internal 8 | Complete |
| 15.6 Drop policy | external 5.3、internal 8 | Complete |
| 15.7 SDK versions | Dependency baseline below | Complete |
| 15.8 Packaging | external 9、LICENSE_POLICY 5 | Complete |
| 15.9 License gate | LICENSE_POLICY全体、external 9 | Complete |
| 16. Future | external 2.2/11、internal 15/16 | Complete |

### Phase plan

1. Common data types、C++ Core、C ABI、error handling
2. libwebm demux/mux、PTS、finalize
3. libvpx VP9 CPU encode/decode
4. Python API、C# ABI smoke
5. Existing oneVPL decoder integration
6. oneVPL encoder、D3D11/VA-API
7. NVDEC
8. NVENC AV1
9. GPU copy/zero-copy transcode
10. C# high-level wrapper
11. SVT-AV1/libaom、future media features

### Existing assets

`C:\Users\masaki\Documents\vpl_wrapper`から次を再利用候補とする。

- oneVPL decoder
- libwebm VP9 demuxer
- `SurfaceFrame` lease
- CPU/GPU memory mode
- bounded prefetch queue
- batch read
- device enumeration
- pybind11/type hints/test structure

既存APIを直接正本にせず、新C ABI/Core規則へ適合させて移植する。

### Dependency baseline

| Dependency | Baseline |
|---|---|
| oneVPL Dispatcher | API 2.10+ |
| NVIDIA Video Codec SDK/API headers | 13.1系列 |
| NVIDIA driver | 対応API以上、runtime query必須 |
| CUDA build toolkit | 13.1系列（CUDA buildのみ） |
| SVT-AV1 | 4.x |
| libvpx | 1.17.x |

## LICENSE_POLICY.md

| Legacy section | New canonical link | Coverage |
|---|---|---|
| 1-4: conclusion/component/linking | LICENSE_POLICY.md remains canonical、external 9、internal 9/14 | Complete |
| 5: artifact contents | LICENSE_POLICY remains canonical、TEST-PKG-001..003 | Complete |
| 6: compliance automation | TEST-PKG-001..005、quality gate | Complete |
| 7: user-facing attribution | EXT-PKG-005..006、TEST-PKG-005 | Complete |
| 8-9: release Go/No-Go | EXT-PKG-006、AC-PKG-001 | Complete |
| 10: next actions | external Open Questions、internal Open Questions | Complete |


