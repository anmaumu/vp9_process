# MKVCodec ライセンス・再配布方針

- ステータス: Draft 0.1
- 調査日: 2026-08-29
- 対象: Windows/Linux向けwheel、NuGet、native SDK、ソース配布

> 本文書は技術・リリース工程上の方針であり、法的助言ではない。正式な商用公開、顧客への保証、特許訴訟を行う組織での採用前には、対象国・事業形態を含む専門家レビューを行う。

## 1. 結論

本プロジェクトは、次の条件を守れば、permissiveなOSS依存を中心に配布可能な構成にできる。

1. H.264/HEVCを実装・公開・暗黙選択しない。
2. VP9/AV1でも「無条件に特許リスクがない」とは表示しない。
3. libvpx/libwebm/libaom/SVT-AV1のLICENSEとPATENTSを配布物へ含める。
4. AOM Patent License 1.0の再掲載、Necessary Claims、defensive terminationを組織として受け入れられるか確認する。
5. NVIDIA Video Codec SDKのsample codeやSDK packageを製品へコピーしない。
6. NVIDIA API定義はMIT表記の `nv-codec-headers` を使用し、driver APIを動的ロードする。
7. NVIDIA/Intel/CUDA/display driverをwheelやNuGetへ同梱しない。
8. oneVPL DispatcherのみMIT条件で同梱候補とし、Intel GPU runtime/driverは原則system dependencyとする。
9. Matroska/WebM/NVIDIA/Intel等のlogoを使用せず、提携・認定を示唆しない。
10. `MKVCodec` は公開前の作業名とする。商用公開前にMatroskaへ名称利用を確認するか、中立的な製品名へ変更する。
11. release artifactごとにSBOM、third-party notices、LICENSE/PATENTS収録検査を自動化する。

## 2. 推奨するプロジェクト自身のライセンス

オープンソースとして公開する場合、MKVCodec自身のコードには **Apache License 2.0** を推奨する。

理由:

- 商用利用、変更、再配布を許容するpermissive licenseである。
- contributorから利用者への明示的なpatent grantとdefensive terminationがある。
- BSD/MIT系依存と通常組み合わせやすい。
- C ABI、Python、C#の各利用者に同じ条件を提示できる。

ただし、次は別契約のままであり、Apache-2.0へ吸収されない。

- AOM Patent License 1.0
- WebM ProjectのAdditional IP Rights Grant
- NVIDIA/Intel driverの利用条件
- 各third-party componentのLICENSE/PATENTS

プロジェクトを非公開・商用ライセンスにする場合でも、third-party noticesとpatent licenseの再掲載義務は残る。最終的なproject licenseは権利者が選択し、`LICENSE`追加前に確定する。

## 3. 依存コンポーネント評価

### 3.1 配布予定の主要依存

| Component | 用途 | 主な条件 | リスク | 方針 |
|---|---|---|---|---|
| libvpx | VP9 CPU encode/decode | BSD-3-Clause系 + WebM patent grant | 中 | LICENSE/PATENTS同梱、version固定 |
| libwebm | WebM/Matroska mux/demux | BSD-3-Clause系 + WebM Project patent notice | 低～中 | LICENSE/PATENTS同梱、静的リンク可 |
| libyuv | CPU色変換 | BSD-3-Clause系 | 低 | LICENSE/PATENTSが存在するreleaseでは両方同梱 |
| libaom | AV1 CPU decode | BSD-2-Clause系 + AOM Patent License 1.0 | 中 | LICENSE/PATENTS同梱、AOM条件レビュー |
| SVT-AV1 4.x | AV1 CPU encode | BSD-3-Clause-Clear + AOM Patent License 1.0 | 中 | LICENSE/PATENTS同梱、AOM条件レビュー |
| Intel libvpl | oneVPL API/Dispatcher | MIT | 低 | Dispatcherのみ同梱候補 |
| Intel vpl-gpu-rt | Intel GPU runtime | MITだがdriver stack依存 | 低～中 | 原則system dependency、同梱しない |
| nv-codec-headers | NVDEC/NVENC API header | 各headerにMIT形式の許諾 | 低～中 | SDK sampleを使わず、必要headerのみ使用 |
| NVIDIA driver API | NVDEC/NVENC/CUDA実行 | NVIDIA driver/EULA | 中 | system dependency、動的ロード |
| pybind11 | Python binding | BSD-3-Clause | 低 | notice同梱 |
| NumPy | Python配列 | BSD系 | 低 | Python dependency。version metadataに記録 |

「低」は義務がないという意味ではなく、通常はattributionとlicense text同梱で管理可能という意味である。「中」はpatent条項、vendor EULA、商標、または再配布範囲に法務判断が残ることを示す。

### 3.2 libvpx / VP9

libvpxのsoftware licenseは、source/binary再配布を許可するBSD-3-Clause型である。binary配布ではcopyright notice、条件、免責をdocumentation等へ再掲載する必要があり、Google/WebM Project/contributor名を無断でendorsementへ使用できない。

別ファイルのPATENTSは、Googleがlicensableで実装に必須となるpatent claimsについて無償grantを示す。一方で:

- grant主体・対象claimsには範囲がある。
- 第三者の全patentを包括する非侵害保証ではない。
- WebM implementationに関するpatent litigationを開始した場合のtermination条項がある。

方針:

- `libvpx/LICENSE` と `libvpx/PATENTS` をartifactへそのまま収録する。
- 「royalty-free」「patent-free」を製品保証として表示しない。
- VP9関連patent訴訟を行う可能性がある法人では事前に法務確認する。
- libvpxを改変した場合はsource noticeを保持し、変更一覧をSBOMへ記録する。

### 3.3 libaom / SVT-AV1 / AV1

libaomはBSD-2-Clause系、SVT-AV1 0.9以降はBSD-3-Clause-Clearであり、いずれもAOM Patent License 1.0を伴う。

AOM Patent License 1.0の重要点:

- Necessary Claimsについてworldwide/no-chargeのpatent grantがある。
- binaryを含むImplementation配布時にlicenseをdocumentation/legal notices等へ再掲載する必要がある。
- 配布者自身が持つNecessary Claimsを同licenseで利用可能にする条件がある。
- ImplementationのNecessary Claimsを主張するpatent litigationにはdefensive terminationがある。
- non-infringement warrantyではない。

この条件は単なるcopyright attributionより重要である。特にcodec関連patent portfolioを持つ会社、親会社・関連会社を含むpatent litigation方針がある会社では必ず確認する。

方針:

- libaom/SVT-AV1のLICENSEとPATENTSをwheel/NuGet/native SDKへ収録する。
- AV1 software backendだけでなく、NVENC/oneVPLでAV1 bitstreamを生成・処理する配布物にもAOM Patent Licenseをlegal noticesへ含める。
- `THIRD_PARTY_NOTICES.md`から各原文へ明確に導線を設ける。
- SVT-AV1 4.xのmajor APIを固定し、license file hashもlockする。
- AOM Patent Licenseを受け入れられない組織向けにAV1をcompile-timeで除外できるbuild optionを設ける。

### 3.4 libwebm / Matroska / WebM

libwebmはpermissiveなsoftware licenseとpatent noticeを持ち、binary同梱は可能と評価する。ただし、format利用と名称/logo利用は別問題である。

Matroska公式サイトはformat技術をopen/freeと説明する一方、Matroskaの名称とlogoについて、非商用以外では許可・確認を求める方針を示している。

方針:

- `.mkv`, `.webm`, `Matroska`, `WebM` は互換性説明に必要な範囲でplain textとして使用する。
- Matroska/WebMのlogo、official icon、file association iconを同梱しない。
- 「official」「certified」「sponsored」「partner」等を表示しない。
- 公開製品名 `MKVCodec` はMatroska側へ商用利用を確認するまで確定しない。
- 許可を取得しない場合、neutralなbrand名へ変更し、「Matroska/WebM compatible」を説明文で使用する。
- Matroska validator/test suiteを受け入れ試験に使用する。

### 3.5 Intel oneVPL

Intelのlibvpl Dispatcher/APIとvpl-gpu-rtはMITで公開されている。copyright noticeとpermission noticeを保持すれば、source/binary再配布は比較的扱いやすい。

ただしIntel GPU実行には、Windows/LinuxのGPU runtime、Linuxではlibva/media driver等のsystem stackが必要になる。

方針:

- `libvpl` Dispatcherはversionと依存を固定した上でwheel/NuGetへの同梱候補とする。
- Intel GPU runtime、media driver、OS graphics componentsは同梱しない。
- Linux native VA同期はhostの`libva.so.2`/`vaSyncSurface2`をruntime loadする。libvaはMITのsystem dependencyとしてmanifest/SBOMへ記録し、wheel/NuGetへbinaryやvendor headerを同梱しない。利用者のdriverがentry pointを実装しない場合は非対応とする。将来同梱へ変更する場合は別途notice・依存license・再配布を再レビューする。
- systemにあるruntimeをDispatcher経由で検出する。
- `Intel`, `oneVPL`のlogoを使用せず、対応backend名としてplain textで正確に記載する。
- Intelによる認定・推奨を示唆しない。
- Dispatcherを同梱するartifactにはlibvplのMIT licenseとthird-party-programs noticeを収録する。

### 3.6 NVIDIA NVDEC/NVENC/CUDA

NVIDIA Video Codec SDK EULAは、SDK sample由来codeの配布条件、stand-alone SDK再配布禁止、notice、protective terms等を定め、codec patent rightsをSDK licenseが付与しない旨を明示する。

一方、FFmpegの `nv-codec-headers` に収録されるNVIDIA API headersは、headerごとにMIT型のpermission noticeを持つ。これを利用すると、SDK sample/base classを製品へ持ち込まずAPIを呼び出せる。

採用方針:

```text
Build:
  nv-codec-headers（必要headerのみ）

Runtime:
  Windows: nvEncodeAPI64.dll / nvcuvid.dll / nvcuda.dll等をsystemから動的load
  Linux: libnvidia-encode.so / libnvcuvid.so / libcuda.soをsystemから動的load

Not distributed:
  NVIDIA driver
  CUDA driver/runtime package
  Video Codec SDK zip
  SDK sample source/base classes/stub libraries
```

- SDK sampleをcopy/pasteしない。必要なwrapperはclean implementationとする。
- `nv-codec-headers`の対象versionをlockし、各headerのlicense noticeを保持する。
- 初期lockは `n13.1.15.0`（archive SHA-256 `2255bc74d038b95aa4be30f5f66322c2176acbdb90ada1851db6993536fbeaf7`）とする。
- NVIDIA runtime libraryがない環境でもMKVCodecをload可能にする。
- NVIDIA backendの利用規約を製品licenseへ誤って再許諾しない。
- NVIDIA EULAが要求する追加条件が発生するartifactを作らない。
- AV1 codec patentについてNVIDIA EULAに依存しない。AOM Patent License側で別管理する。
- NVIDIA/CUDA/NVENC/NVDECのlogoを使用せず、互換性説明としてplain textで使用する。

SDK 13.1由来sample/base classをどうしても使用する場合、そのNVIDIA license noticeと配布条件を別途精査し、そのmoduleをOSS Coreから分離する。初期方針では使用しない。

## 4. Linking・プラグイン境界の方針

### 4.1 OSS codec/container

libvpx、libwebm、libyuv、libaom、SVT-AV1はpermissive licenseであるため、license/patent noticesを守る前提でstatic linkを許可する。

利点:

- wheel/NuGetを自己完結させやすい。
- DLL search path問題を減らせる。
- 利用者のcodec library version差を避けられる。

各static libraryのversion、build flags、source commit、license hashをSBOMへ記録する。

### 4.2 Vendor GPU runtime

Intel/NVIDIA/OS driverはstatic link・同梱しない。adapterとvendor runtimeの境界は動的loadとcapability queryにする。

```text
MKVCodec OSS Core
  +-- CPU libraries: artifact内
  +-- Intel adapter: libvpl Dispatcher -> system GPU runtime
  `-- NVIDIA adapter: dynamic symbols -> system NVIDIA driver
```

この境界はlicenseだけでなく、GPUなし環境でのimport、driver更新、複数GPU対応にも有利である。

## 5. Artifact別の収録方針

### 5.1 Python wheel

wheelには最低限次を収録する。

```text
mkvcodec/
  native MKVCodec library
  Python binding
mkvcodec-*.dist-info/
  METADATA
  licenses/
    MKVCodec-LICENSE.txt
    THIRD_PARTY_NOTICES.md
    libvpx-LICENSE.txt
    libvpx-PATENTS.txt
    libwebm-LICENSE.txt
    libwebm-PATENTS.txt
    libyuv-LICENSE.txt
    libyuv-PATENTS.txt（採用revisionに存在する場合）
    libaom-LICENSE.txt
    libaom-PATENTS.txt
    SVT-AV1-LICENSE.txt
    SVT-AV1-PATENTS.txt
    libvpl-LICENSE.txt（同梱時）
    nv-codec-headers-LICENSES.txt（NVIDIA adapter同梱時）
    pybind11-LICENSE.txt
  sbom.spdx.json
```

NumPyは通常wheelのdependencyとして別配布されるためbinaryを内包しない。ただしbuild時に使用したversionをSBOMへ記録する。

### 5.2 NuGet

NuGet packageには:

- `runtimes/win-x64/native/mkvcodec.dll`
- 将来のLinux runtime asset
- managed wrapper
- project license
- `THIRD_PARTY_NOTICES.md`
- `licenses/`以下のLICENSE/PATENTS原文
- SBOM

を収録する。NuGet metadataのlicense expressionはMKVCodec自身のlicenseのみを示し、third-party licensesを隠さないようnoticesへのlinkを説明へ含める。

### 5.3 Source distribution

- project `LICENSE`
- root `THIRD_PARTY_NOTICES.md`
- vendored sourceごとのLICENSE/PATENTS原文
- source URL、commit/tag、patch一覧
- dependency取得script
- reproducible build manifest

を含める。AOM Patent Licenseが求めるsource rootでのlicense配置を満たすよう、AV1実装を含むsource distributionではrootからPATENTS原文へ直接到達できる構成にする。

## 6. 自動化するcompliance gate

release CIに次を追加する。

1. dependency lockfileからSBOMを生成する。
2. 各dependencyのLICENSE/PATENTS hashをallowlistと比較する。
3. license fileの欠落があればrelease buildを失敗させる。
4. wheel/NuGet/zipを展開し、必要noticeの実収録を検査する。
5. 禁止binary名を検査する。
   - NVIDIA/Intel display driver
   - CUDA Toolkit/runtime package（明示承認なし）
   - Video Codec SDK sample/stub artifact
6. `strings`/symbol scanでH.264/HEVC encoder GUID・公開API・設定名の混入を検査する。
7. source scanでNVIDIA SDK sample由来copyright noticeを検出し、承認なしなら失敗させる。
8. package metadataと実際の内容物を照合する。
9. attribution文にendorsementを示唆する表現がないか検査する。
10. release approval checklistへ法務review IDを記録する。

## 7. 利用者向け表示

READMEとpackage descriptionでは次のように記載する。

- VP9/AV1、Matroska/WebM対応を技術的事実として説明する。
- 対応可否はhardware/driver capabilityに依存すると説明する。
- Intel/NVIDIA/Google/AOM/Matroskaからの認定・提携を示唆しない。
- third-party marksは各所有者に帰属すると表示する。
- codec・patent・輸出管理・用途規制について利用者自身の確認が必要な場合があると表示する。
- safety-critical用途に関するvendor免責を上書きする保証をしない。

推奨文例:

```text
MKVCodec is an independent project and is not endorsed by Intel, NVIDIA,
Google, the Alliance for Open Media, or the Matroska project. Third-party
names and marks are the property of their respective owners.
```

## 8. リリース可否判定

### 8.1 OSS/社内評価版

次を満たせば技術previewを配布可能と判断する。

- permissive dependencyのみを使用
- LICENSE/PATENTS原文を収録
- vendor driver/SDK packageを同梱しない
- H.264/HEVCを含まない
- SBOMを生成
- trademark logoを使用しない
- previewであり法的保証をしない旨を明記

### 8.2 商用・一般公開版

次を追加で必須とする。

- project自身のlicense/利用規約確定
- AOM Patent Licenseの組織レビュー
- VP9/WebM patent grantの組織レビュー
- Matroskaを含む製品名の許可またはneutral nameへの変更
- NVIDIA API header/runtime利用方式のレビュー
- 対象販売国・顧客用途を踏まえたcodec patent確認
- export control、sanctions、safety-critical用途の確認
- 法務review IDをrelease recordへ保存

## 9. 現時点のGo/No-Go

| 項目 | 判定 | 条件 |
|---|---|---|
| CPU VP9 prototype | Go | libvpx/libwebm LICENSE/PATENTS収録 |
| CPU AV1 prototype | Go with review | AOM Patent License条件を受け入れること |
| Intel GPU prototype | Go | libvpl MIT、runtimeはsystem dependency |
| NVIDIA GPU prototype | Go | nv-codec-headers + dynamic driver load、SDK sample不使用 |
| public wheel/NuGet | Conditional | artifact notice/SBOM gate完成 |
| `MKVCodec`名で商用公開 | No-Go pending | Matroskaへ確認または名称変更 |
| 「patent-free」の表示 | No-Go | 非侵害保証になり得るため使用しない |
| H.264/HEVC追加 | No-Go | 現仕様で明示的に除外 |

## 10. 次に実施する作業

1. project licenseをApache-2.0にするか権利者が決定する。
2. 公開brand名をneutral nameへ変更するかMatroskaへ問い合わせる。
3. `third_party/manifest.toml` 等でversion、URL、license、patent noticeを固定する。
4. `THIRD_PARTY_NOTICES.md` と `licenses/` を生成するscriptを作る。
5. NVIDIA adapterは `nv-codec-headers` のみでclean implementationする。
6. release artifact inspection testをCIへ追加する。
7. 商用公開前にAOM/VP9 patent条項と対象国について専門家レビューを受ける。
