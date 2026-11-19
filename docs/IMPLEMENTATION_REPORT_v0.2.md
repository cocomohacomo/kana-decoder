# SURGE Core 実データ評価準備レポート v0.2

日付: 2026-11-19  
対象ブランチ: `refactor/surge-core-as-runtime`  
対象仕様: `SPECIFICATION_v0.43.md`

## 1. 今回実装した範囲

### SURGE Core

- vendored SURGE Coreの `UnifiedDecision` を利用し、語彙解釈と表層候補を単一の決定境界で選択する。
- RecognitionとNormalizationに独立したDecision APIを設けず、同一policy・provenance・budget・trace・commit境界に統合した。
- 候補数、候補あたり証拠数の上限を設定可能にした。
- 無効証拠を得点対象から除外し、有効証拠がない候補は選択しない。
- 同一のEvidence IDの重複を拒否する。
- 同一family/correlation groupの証拠は絶対寄与が最大の1件のみ加点し、相関証拠の単純な重複加点を抑制する。
- 同点時はstable_order、candidate_idの順で決定し、決定を再現可能にする。
- 証拠不足、曖昧なmargin、予算超過はDEFERとして返す。

### kana-decoder統合

- vendored SURGE CoreにDecision APIを取り込み、ビルド対象に追加。
- `SurgeNormalizationRuntime` がN-best候補ごとに証拠パケットを構築し、SURGE `RecognitionDecision` で最終経路を選択するよう変更。
- デコーダーの1位候補を無条件採用する `paths.front()` の最終選択依存を除去。
- 選択された語彙解釈に対し、表記候補を `NormalizationDecision` で別途決定する。
- 正規化決定の候補IDをジャーナルのコミットペイロードに記録する。
- Coreがデコーダー1位以外の候補を選択する回帰テストを追加。

## 2. 実データ評価CLI

新規ツール: `kana_decoder_surge_eval`

ビルド後の実行例:

```bash
./build/kana_decoder_surge_eval packs/ja-lite /path/to/corpus.tsv test
```

引数:
1. Language Packディレクトリ
2. 評価コーパスTSV
3. split名。省略時は `test`

対応入力形式:

- 3列形式: `reading<TAB>expected_surface<TAB>domain`。1行を1評価ケースとして扱う。
- 6列形式: `document_id<TAB>sentence_id<TAB>reading_segment<TAB>surface_segment<TAB>metadata<TAB>split`。同一document_id/sentence_idの連続行を結合して文単位に評価する。

評価時に正解テキストや正解トークン境界をデコーダーへ渡さない。正解は最終出力との比較にのみ使用する。

出力JSON:
- Decoder top-1 CER / sentence exact
- SURGE Core CER / sentence exact
- SURGEによるtop-1変更数
- DEFER数、Runtime失敗数、Decode失敗数
- 経過秒、文/秒、入力1,000コードポイントあたり秒
- Peak RSS (LinuxではKiB、Windowsでは未計測値 -1)

評価ケースごとにSURGE Runtimeとジャーナルを新規作成・破棄し、文脈グラフがコーパス全体に累積しないようにした。評価メモリを文数に比例して増やさないための措置である。文をまたぐ文脈効果の評価は、別途ストリーム評価モードとして追加する。

## 3. CIに追加した検査

- SURGE Core Decision APIの単体テスト。
- decoder top-1をCoreが覆す統合テスト。
- bundled Japanese smoke corpusによる `kana_decoder_surge_eval` の起動・実行テスト。

## 4. 実データ評価の実施条件

リポジトリに含まれる `tests/data/ja-smoke-gold.tsv` は4行のスモークデータであり、CERや実用精度を論じるための実データではない。大規模な評価結果として扱ってはならない。

実データ評価には、別途用意した正解付きコーパスを上記CLIへ渡す。既存の6列形式コーパスをそのまま利用できる。データセットのsplit、正規化規則、句読点の扱い、評価対象文字範囲を固定し、Decoder top-1とSURGE Core出力を同じ条件で比較する。

## 5. 未完了事項と評価上の制約

この段階は「Coreによる候補決定を通した実データ評価が可能な入口」であり、Language Adapter / Normalization AdapterのSURGE完全移管完了ではない。

未完了:
- 決定スコアのfamily別校正とdev splitでの重み選択。
- Language Set ABI、Manifest Loader、session pinningの実行実装。
- 日本語辞書・形態素・接続・N-gram・正規化リソースのSURGE Language Setへの完全移管。
- rendererをLanguage Set内で実行し、Coreにrender conflictを返す実装。
- Question/Interest/Focusや時間的再評価の結果をDecision APIの得点・保留条件へ正式接続すること。
- 2つ目の異言語Language Setによる交換可能性試験。
- 実データでのCER、文完全一致、RTF相当、Peak RSS、Packサイズ、ロード時間の比較。

現在のCLIはテキストコーパスの処理時間を測定する。音声時間を持たないためRTFは算出しない。実運用RTFは音声長または入力供給時間を伴うベンチマークで別途測定する。

## 6. 完了判定

- [x] SURGE CoreにRecognitionDecision / NormalizationDecisionを追加
- [x] kana-decoderの最終経路選択をCore決定へ接続
- [x] top-1覆しの回帰テストを追加
- [x] 実データ評価CLIを追加
- [x] CIビルド・テスト成功を確認（コード変更head `79046d8`、後続head `a4fef5b` はUPSTREAM.mdのみの文書変更）
- [ ] 大規模実データコーパスで比較評価を実行（UD Japanese-GSD workflowへ接続済み、実行結果待ち）
- [ ] 性能・メモリ・精度ゲートを確定
- [ ] Language Set完全移管と2言語交換試験


## 7. UD Japanese-GSD 実データパイプラインへの接続

既存の `.github/workflows/sudachi-pack.yml` が固定コミット
`c662dc96ed5ee5ac0fc8b5e44261557e431f50e6` の
UniversalDependencies/UD_Japanese-GSDを取得し、Sudachi Small辞書に含まれる連続区間を
train/dev/testへ分割して整形する。今回このworkflowに以下を追加した。

- `kana_decoder_surge_eval` をビルド対象に追加。
- train splitから生成された `train_sequences.tsv` を評価器へ渡す。
- test splitのみをSURGE評価に使用し、学習データと評価データを分離。
- SURGE評価器のPeak RSSも既存の1,792 MiBゲートで検査。
- SURGE評価JSONと `/usr/bin/time -v` ログをCI出力へ表示。

実データ評価コマンド:

```bash
./build/kana_decoder_surge_eval \
  /path/to/ja-jp-sudachi-small \
  /path/to/tokenized.tsv \
  test \
  /path/to/train_sequences.tsv
```

評価器は学習列からword unigram、left bigram、left trigram、skipgramとPOS系列を構築する。
評価時はtest splitのreadingだけをデコーダーへ渡し、SURGE Coreの認識・正規化決定後の出力をgold surfaceと比較する。
元データの再配布は行わず、従来workflowと同じ取得・ライセンス管理を維持する。

このworkflowの変更はPRブランチに追加済みだが、mainへのマージやworkflow_dispatchは実行していない。


## 8. CI検証結果

kana-decoder PR #1 のコード変更head `79046d8ddaa1d1d2e44920dd629f27d2c972ae21` でGitHub Actionsが成功した。

- C++ build: success
- CTest: 26/26 passed（`surge_core_normalization_integration` と `surge_core_real_data_eval_smoke` を含む）
- Python tests: 71件、66 passed / 5 skipped
- bounded decode microbenchmark: success
- bounded SURGE runtime microbenchmark: success
- Japanese reference Pack verification: success

Run: https://github.com/cocomohacomo/kana-decoder/actions/runs/44502736958

SURGE PR #139 のCore Decision API側も、head `7e8ba7c80df8bcc4c487ccf14d7b99abbe1200b3` でCI成功を確認した。

この結果は実装・統合・スモーク評価の合格を示す。UD Japanese-GSD全量を使った大規模test評価は、既存のSudachi Pack workflowに接続済みだが、workflow自体はまだ実行していない。


## 9. Dev-only decision policy calibration

SURGE Core DecisionPolicy now accepts optional evidence-family multipliers. Effective evidence contribution is:

`evidence.value × evidence.weight × family_multiplier`

Missing family entries retain multiplier 1.0. Invalid negative/non-finite values and overflowed contributions are rejected. The runtime exposes family multipliers and renderer score as explicit configuration, and the evaluator can load a two-column TSV policy file:

```text
decoder_prior<TAB>0.5
lexical<TAB>1.0
morphology<TAB>1.0
transition<TAB>1.0
ngram<TAB>1.5
pos_ngram<TAB>1.0
context_cluster<TAB>1.5
normalization_renderer<TAB>0.5
```

New tool: `tools/calibrate_surge_policy.py`

- Runs a fixed, bounded five-preset grid on dev only.
- Enforces the 1,792 MiB Peak RSS ceiling on every calibration preset; missing RSS measurement fails closed.
- Selects minimum CER, then maximum sentence exact, then fixed preset order.
- Writes the chosen TSV policy and calibration JSON.
- Does not read the test split.
- The Sudachi Pack workflow now calibrates on dev, freezes the selected policy, and evaluates test with that policy.
- Test evaluation JSON records the exact family weights and renderer score used.

This first grid is a reproducible baseline, not a claim of optimal calibration. It is intentionally small to bound CI runtime. A more granular search should be justified by dev results and must retain the test holdout.

### New completion gates

- [x] Configurable evidence-family multipliers in SURGE Core
- [x] Runtime injection and evaluator policy loading
- [x] Deterministic dev-only calibration tool
- [x] Held-out test evaluation with frozen policy wired into workflow
- [x] Unit test for dev-only grid selection, deterministic tie-break and provenance hashes
- [ ] CI verifies calibration script and policy loader end-to-end
- [ ] Real UD Japanese-GSD dev/test metrics and resource use collected


## 10. Unified SURGE decision and clustered input retention

Recognition and normalization are now exposed through one Core API,
`surge::UnifiedDecision::evaluate`. A path candidate carries both
interpretation evidence and its source/rendered surface hypotheses. SURGE
selects the interpretation and all of its surface realizations in one call,
under one policy and one trace/disposition boundary. If any surface set cannot
be resolved, the unified result defers/rejects and clears partial surface
selections. The old `RecognitionDecision` and `NormalizationDecision`
entry points are removed from this implementation.

Input retention is also made explicit in SURGE Core. The new
`ContextGraph::append_cluster_membership` API deterministically classifies
members under typed child clusters and adds parent/member containment edges to
the same SemanticDelta. kana-decoder supplies only classification labels and
partition keys:
- `LEXICAL_INTERPRETATION` clusters retain candidate Claims.
- `SURFACE_REALIZATION` clusters retain source and language-rendered surface Entities.
- Claims retain linked Evidence, source digest, validity and provenance.
- Contradictions and unresolved alternatives continue into Question routing
  and Chaos Cluster retention.

The runtime reports the persisted Cluster count. The runtime conformance test
now asserts that root, interpretation and surface clusters exist. SURGE
ContextGraph conformance tests cover deterministic cluster membership and
recovery after journal replay.

### Verification status
- [x] Unified Core API and adapter integration committed to the open PR branches.
- [x] Cluster classification delegated from the adapter to SURGE Core API.
- [x] Conformance tests added for unified surface choice and cluster replay.
- [ ] CI for the latest SURGE and kana-decoder commits is pending.
- [ ] Real UD Japanese-GSD dev calibration and held-out test metrics remain pending.


## 11. Bounded cross-cluster retrieval and dependency-aware reassessment

The next retrieval stage is implemented in the kana-decoder integration branch
without modifying the SURGE main-development repository.

- Reads the current Core Focus and traverses at most 16 child or explicitly
  related Clusters, with depth capped at 2. It does not walk upward to the
  session root, preventing accidental retrieval of unrelated sibling topics.
- Reads a bounded relation window (up to 8,192 relations, from both ends of the
  ordered index) and reports scanned count/truncation.
- Retrieves retained Claim surfaces from those Clusters and injects
  `active_focus` evidence into the single `UnifiedDecision`.
- Builds the latest ClaimValidity view and invokes the existing vendored
  `DependencyEngine::evaluate` with limits of 128 impacted nodes and 2,048
  predicate operations. The pass is skipped when the graph exceeds 4,096 nodes;
  this skip is exposed as a runtime metric.
- When a focused historical Claim is dependency-impacted, adds
  `dependency_reassessment` evidence to the matching current candidate.
- Routes Claim contradictions to their interpretation Cluster (not the
  session root), allowing QuestionRouter to set Focus on the relevant semantic
  Cluster.
- Exposes retrieval counts, dependency trigger/impact counts and truncation
  state through runtime metrics.

The adapter does not create a second persistent context store. All retrieved
records, Focus, Questions, Dependencies, validity and Cluster memberships are
read from the vendored SURGE Core graph/journal. The vendored Core snapshot is
not edited in this stage; the SURGE main repository remains untouched.

### Added regression coverage
A two-packet test creates a contradiction and Focus in the first packet, then
presents an otherwise tied previously retained surface against a new surface.
The second packet must select the retained Focus-supported surface and report
retrieved Claims, dependency triggers/impacts and bounded relation scanning.

### Verification status
- [x] Adapter implementation and regression test committed.
- [ ] Latest C++/Python CI and real-data evaluation are pending.


## 12. 目的再確認と正文化経路のリファクタ

本リポジトリの製品目的は、純かな入力を、文脈に適合した漢字・かな・カナ・英字・数字・記号・句読点を含む正文へ変換することである。SURGE Core自体の機能拡張やグラフ構築を成果目標に置き換えてはならない。評価の最終指標は、同一条件で測定したCER、文完全一致率、ストリーミング確定品質、処理時間、Peak RSSおよび配布サイズとする。

この目的に照らして、今回以下をリファクタした。

- Focusからの関連Cluster探索で16 Cluster上限に達した後も、上限超過となる隣接Clusterを検出し、`retrieval_truncated` を正しく立てるよう修正。従来の `cluster_queue.size() > 16` 判定は、enqueue上限と同じ値でqueueを制限していたため成立せず、Cluster上限による探索打ち切りを見逃していた。
- CI workflow内で重複していたpolicy calibratorのPython構文検査を1回に整理。
- 本レポート冒頭のDecision API説明を、現行のUnifiedDecision統合仕様に整合させた。

### 製品目的に対する未完了ギャップ

現時点で、候補生成・形態素解析・辞書/N-gram参照・日本語数詞解析などの一部はkana-decoder側のC++実装に残っている。また、C APIは従来のLexiconTrie / Scorer / ConnectionModelを直接保持し、SURGE Runtimeへ候補を渡す構造である。したがって、v0.43仕様が掲げる「Language AdapterとNormalization AdapterのSURGE Language Runtime / replaceable Language Setへの完全移管」は未完了であり、現状を完全移管済みと扱わない。

次のリファクタは、SURGE本体をこのリポジトリから変更せず、kana-decoder側で次の順に進める。

1. 日本語Packの全資源をLanguage Set単位でロードできる境界を確立し、辞書・形態素・接続・N-gram・表記規則を単一のPack契約に束ねる。
2. 数詞・日付・時刻・単位・句読点・表記変換を含む日本語レンダラーをPack側へ集約し、Runtimeはrendererの結果を候補EvidenceとしてUnifiedDecisionへ渡すだけにする。
3. C/Python APIは入力・出力・互換性に限定し、Host側に第二の候補選択・表記選択ロジックを残さない。
4. 第二言語PackでABI交換試験を行い、言語固有処理がCoreやHostへ漏れていないことを検証する。
5. 大規模正解付きコーパスでDecoder基準値とSURGE統合後を同一分割・同一正規化規則で比較し、精度改善とメモリ/遅延予算の両立を確認する。

この段階の変更はkana-decoderブランチ内に限定し、SURGE本体リポジトリおよび別セッションのSURGE開発ブランチには変更を加えていない。


## 13. Immutable Language Pack runtime boundary (v0.44 stage)

Implemented in kana-decoder only:

- Added the read-only `LanguagePack` contract exposing pack identity, decode/stream-append providers and Language Adapter. The Japanese implementation privately owns the lexicon, n-gram scorer, connection model and correction resources.
- Added a generic manifest dispatcher and moved the concrete Japanese provider implementation into `language_sets/ja-JP/japanese_language_pack.cpp`. The Japanese Pack snapshot loader verifies the existing manifest and payload hashes, loads resources once, records pack ID/language/version and computes a SHA-256 digest over the manifest bytes.
- Changed the C API engine to hold `shared_ptr<const LanguagePack>` instead of owning separate dictionary/scorer/connection fields. The C API now calls only Pack-mediated decode and append operations.
- Changed streaming sessions to pin the same immutable shared snapshot instead of copying large lexicon and model structures.
- Rejected a second Pack load into an already-loaded engine, preventing silent pack mutation while sessions may exist.
- Added Language Adapter entity rendering and routed Japanese numeric parsing/rendering through the adapter bound to the SURGE normalization runtime. The generic SURGE runtime no longer directly calls the Japanese number parser or renderer.
- Added Language Pack conformance coverage for identity, digest, adapter tag, decode and streaming providers, shared snapshot identity and numeric realization. Added C API checks for one-time Pack pinning and adapter preservation after stream reset.
- Added the new source to both static and shared library targets.

### Scope and limitations

This is a migration boundary, not full Language Set completion. The current dispatcher constructs the Japanese implementation only. Its provider source is now separated under `language_sets/ja-JP/`, but is still statically compiled into the kana-decoder library. Although host code invokes Pack-mediated decode/append operations, the underlying lattice/morphology engine remains in the existing shared decoder implementation and dynamic module loading is not yet available. Most candidate scoring and non-numeric surface realization are still migration work. The internal C++ interface is not yet a stable external plugin ABI. No second language pack has been implemented.

### Verification

GitHub Actions is triggered for the updated PR head. Build/test success, production dictionary memory impact, load-time impact, CER and sentence exact-match impact must be reported only after the corresponding runs and representative corpus measurements complete. The small `ja-lite` fixture test is conformance coverage, not a production-quality benchmark.

SURGE main repository remains untouched; its vendored snapshot was not modified.


## 14. Japanese Language Set build isolation (v0.45)

今回の実装で、`MorphologyDecoder::decode` を `src/morphology_decoder.cpp` から `language_sets/ja-JP/morphology_decoder.cpp` へ移動した。CMakeでは日本語Pack providerと形態素探索を `kana_decoder_ja_language_set` OBJECT targetとしてまとめ、static/shared双方のライブラリが同一オブジェクトを利用する構成に変更した。共有ライブラリへリンクできるようPOSITION_INDEPENDENT_CODEを有効化し、警告をエラーとして扱う既存コンパイル規則も適用している。

この変更は探索アルゴリズムの改変ではなく、実装所有権とビルド境界の移動である。候補順序、スコア計算、探索上限、SURGE証拠利用の挙動は変更していない。したがって、精度・速度・メモリの改善を主張するものではない。移管前後のgolden出力一致、static/shared build、全テスト、bounded benchmarkの確認が必要である。

### 残る移管範囲
- 辞書照合・lattice構築・未知語fallback
- 接続行列、単語/文字/POS N-gram、訂正候補生成
- 全表層実現をLanguage Adapter/Rendererへ移管
- stable external ABIと第二言語Pack
- held-out実データによる品質・性能評価

### 検証状況
- [x] MorphologyDecoder source relocation
- [x] Japanese Language Set CMake OBJECT target
- [x] static/shared両targetから同一objectを利用
- [x] shared library向けPICを設定
- [ ] 最新ブランチのCI build/test完了
- [ ] golden corpusの移管前後完全一致
- [ ] 対象エッジPCでPeak RSS/latency/CPU実測
