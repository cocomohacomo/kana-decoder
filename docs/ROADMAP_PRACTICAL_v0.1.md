# kana-decoder 実用稼働ロードマップ v0.1

更新日: 2026-11-19
対象ブランチ: `refactor/surge-core-as-runtime`
対象仕様: `SPECIFICATION_v0.64.md`

## 0. 製品ゴール

純かな入力を、文脈に適合した漢字・ひらがな・カタカナ・英字・数字・記号・句読点を含む自然な日本語正文へ変換する。SURGEは唯一の文脈認識・候補判断エンジンであり、kana-decoderの成果は正文化品質、ストリーミング品質、処理性能、メモリ、配布容易性で評価する。

## 1. 実用稼働フェーズ

| Phase | 内容 | 主な成果物 | 完了ゲート |
|---|---|---|---|
| P0 基盤安定化 | LanguagePackの不変性、C/Python API、SURGE接続、CI | v0.44 runtime | static/shared build、全テスト成功 |
| P1 日本語認識移管 | lattice、形態素解析、接続行列、辞書、未知語処理をLanguage Setへ移す | 独立ビルド可能なja-JP target | Hostから言語固有実装への直接依存ゼロ |
| P2 証拠生成完成 | 語彙・形態素・遷移・単語N-gram・文字N-gram・品詞N-gram・訂正候補をPack providerからSURGEへ供給 | Evidence provider群 | 同一候補に対する証拠重複・相関加点を抑制、再現可能な判断 |
| P3 文脈認識完成 | Focus、Cluster、Question、Dependency、時間的有効性、再評価 | ストリーム文脈回帰群 | 話題切替、長距離依存、矛盾後の再評価、忘却の各ケース合格 |
| P4 表層実現完成 | 漢字/かな/カナ/英数字、送り仮名、活用、句読点、空白、数詞、日付、時刻、単位、固有表現 | ja-JP Renderer | 意味Claimを壊さず、曖昧時はSURGEへ差し戻す |
| P5 ストリーミング完成 | 増分入力、暫定候補、再評価、確定、リセット、セッション分離 | streaming conformance suite | チャンク分割に対する確定出力の安定性、上限時間・メモリ内 |
| P6 リソース最適化 | 辞書共有/圧縮、ロード遅延、候補数、beam、journal/retrieval予算 | resource profile | 対象エッジPCでPeak RSS/CPU/遅延の予算を満たす |
| P7 実データ評価 | dev校正、固定test、分野別・難例別評価、ablation | benchmark report | CER、文完全一致、かな漢字変換精度、ストリーム確定品質を改善 |
| P8 配布・運用 | Pack manifest/署名・digest、ABI、互換性、診断、オフライン配布 | release bundle | クリーン環境で導入・実行・更新・ロールバック可能 |

## 2. 優先実装バックログ

### P0: 直ちに完了させる
- [ ] 最新PR headのActionsを完了させ、失敗ログを修正する。
- [x] `SemanticDelta::journal_events` の未対応参照を除去し、NORMALIZATION_PROPOSAL/COMMITを同一SemanticDelta内のEvidence nodeとして永続化する。
- [x] 上記修正を含むPR headでstatic/shared buildと全29テストを実行する（CI #1011）。
- [x] Release CTestで `NDEBUG` によってassert式が消えないよう、C/C++テストターゲットへ `-UNDEBUG` を適用。
- [x] `StreamingSession::append` を使用していた日本語依存テストを、provider側decode + `append_decoded` へ移行。
- [x] Japanese Pack loaderの失敗段階を `error` 出力へ伝播し、Pack testのnull dereferenceを診断付き失敗へ変更。
- [x] Sudachi V1 TSV testのタブ区切り文字を修正。
- [x] assertions有効状態で全テストを通し、表面化したテスト不整合を修正。
- [ ] static/shared両方のLanguagePackテストをCIで実行する。
- [ ] C APIでPackロード失敗時にEngine状態が変化しないことを検証する。
- [ ] Engine破棄後もSessionがPack snapshotを安全に保持することを検証する。
- [ ] ストリームreset/journal再接続後もLanguageAdapterが再バインドされることを検証する。

### P1: 次のコード移管
- [x] `MorphologyDecoder` とlattice探索を `language_sets/ja-JP/` に物理移動する。
- [x] 日本語Language Setの形態素探索とPack providerを独立したCMake OBJECT targetとしてビルド可能にする。
- [x] LanguagePackのdecode/append境界を `LanguageDecodeOptions` に変更し、C APIとconformance testを日本語固有 `DecoderOptions` から分離。
- [x] 日本語LanguageAdapter/数詞表記と日本語SURGE Cluster sidecar scorerを `language_sets/ja-JP/` へ移動し、static/shared双方のja-JP OBJECT targetへ統合。
- [x] 日本語decoder型・辞書・scorer・morphology・correction・trace宣言を `japanese_decoder.hpp` へ分離し、generic `engine.hpp` から除去。
- [x] SURGE `LanguageAdapter` をgeneric `Candidate` + score契約へ変更し、日本語Adapter宣言を `japanese_language.hpp` へ分離。
- [x] SURGE選択後の `render_path` 境界を追加し、日本語Rendererのspan検証・任意数詞表記・任意句読点を実装。
- [x] static/shared build、C/Python統合、Renderer統合テストをCIで確認（CI #1011）。
- [x] `engine.hpp` の `Candidate` から日本語固有POS/活用フィールドを除去し、language-tagged `MorphologyDescriptor` へ移行。
- [x] 共通データ型とprovider interfaceを公開ヘッダーへ整理し、Hostから日本語実装ヘッダーを参照させない。
- [x] 辞書Trie、N-gram scorer、接続行列、訂正候補、Pack resource loaderの実装をLanguage Set targetへ移動。
- [x] Pack decodeの決定性、入力上限、UTF-8拒否、チャンク連結、セッション分離をconformance testへ追加。\n- [x] 辞書候補のコスト順序、辞書一致時のfallback抑止、未知語fallbackをfixtureで固定する。
- [x] lattice edge/candidate上限と形態素探索expansion上限の超過を検知し、部分結果を返さないことをfixtureで固定する。
- [ ] 既存の変換結果をgolden corpusで比較し、移管前後の差分をゼロにする。

### P2: 証拠生成と判断品質
- [ ] lexical/morphology/transition/ngram/POS/cluster evidenceを同じ候補ID・spanに紐付ける。
- [ ] N-gramは単語・文字・品詞系列を区別し、スコアの単純二重加点を避ける。
- [ ] 訂正候補は原入力と修正操作をEvidenceとして保持し、確定前にSURGEが比較する。
- [ ] dev splitだけでfamily weightとmarginを校正し、test splitは最終評価専用に固定する。
- [ ] 候補生成漏れと候補順位誤りを分離して計測する。

### P3/P4: 文脈と表層
- [ ] 連続発話で同音異義語が解消されるケースを追加する。
- [ ] 話題切替後に旧Focusが新話題を汚染しないケースを追加する。
- [ ] 矛盾からQuestionが生成され、関連ClusterへFocusが移るケースを追加する。
- [ ] Claim/Evidenceの有効期間切れ、依存先更新、再評価を検証する。
- [ ] Rendererは表層候補と由来Claimを返し、意味を変えるレンダリングは拒否する。
- [x] Sudachi V1 POS5/POS6を活用型/活用形として保持し、`*` sentinelを空値へ正規化。
- [x] 辞書に収録された `回っ` / `食べて` の活用表層・送り仮名を、decodeからRendererまで変更せず保持する回帰テストを追加。
- [ ] 活用形・音便・助動詞連鎖の候補Recallを実辞書とheld-out corpusで計測する。
- [ ] 数字・日付・時刻・単位・通貨・記号・句読点の日本語規則をgolden test化する。

## 3. 品質・性能ゲート

測定条件を固定し、同一コーパス・同一分割・同一正規化規則で比較する。実測値がない項目は合格扱いにしない。

| 指標 | 必須報告 | 判定方針 |
|---|---|---|
| CER | 全体および分野別 | baseline比で改善 |
| Sentence Exact Match | 文単位完全一致率 | baseline比で改善 |
| Candidate Recall@N | 正解形態素/表層が候補に含まれる割合 | 選択器改善前に候補生成の欠落を把握 |
| Streaming Commit Accuracy | 暫定/確定別の正解率、訂正回数 | 確定後の誤りと不要な再確定を抑制 |
| Throughput/Latency | 文字/秒、チャンク遅延、p50/p95 | 対象CPUで予算内 |
| Peak RSS | 最大常駐メモリ | 明示した上限以内。未計測は不合格 |
| Pack size / Load time | 言語Pack別サイズ、ロード時間 | 配布・起動要件以内 |
| Determinism | 同一入力・同一Pack・同一policyの出力 | 再現可能 |

### 暫定リソース予算

- 対象: オフラインCPU-onlyのエッジ業務PC。
- メモリ: まず既存CIの1,792 MiB上限を越えない。最終上限は対象機実測でさらに厳しく設定する。
- 入力・lattice・候補・beam・展開数・journal・Focus retrievalには固定上限を設け、入力長に比例した無制限成長を許さない。
- ストリーミングの性能劣化を防ぐため、辞書・モデルはimmutable Packとして共有し、Sessionごとに複製しない。
- RTFは音声時間または入力供給時間を測定できる条件でのみ算出する。テキスト処理秒数をRTFと呼ばない。

## 4. 評価データと実験規律

1. train/dev/testを文書単位で分離し、同一文書や近似重複が跨がないようにする。
2. 重み・閾値・候補上限はdevで決め、testを見て調整しない。
3. Decoder top-1、N-best、SURGE統合、Renderer統合を段階別に測定する。
4. 辞書/N-gram/Focus/Dependency/Correction/Rendererのablationを実施し、改善寄与とメモリ費用を記録する。
5. 日本語の日常会話、会議、行政文書、技術文書、固有名詞、数字・日付・単位、口語省略、話題切替を分けて報告する。
6. 個人情報を含むデータは権利・利用目的・保管・匿名化を確認し、公開テストデータと混在させない。

## 5. リリース判定

以下を全て満たすまで「実用稼働完了」としない。

- [ ] 日本語Language Setが独立targetとしてビルドされる。
- [ ] SURGEが候補選択と表層選択の唯一の判断主体である。
- [ ] C/Python APIが互換性を維持し、オフラインで動作する。
- [ ] チャンク分割・話題切替・訂正・resetを含むストリーミングテストが合格する。
- [ ] 実データtestでCER、文完全一致、候補Recallを報告する。
- [ ] 対象PCでPeak RSS、CPU、p95遅延、Packロード時間を実測する。
- [ ] 同一条件で再実行した結果が再現する。
- [ ] Packのdigest/version/ABIを検証し、誤ったPackをロードしない。
- [ ] README、API仕様、インストール手順、制約、ベンチマーク報告を揃える。

## 6. 今回の着手範囲

v0.56ではP0のCI・Pack・journal recoveryゲートが通過した。P1ではCandidateの形態情報をprovider-neutralなMorphologyDescriptorへ分離した。次はgolden corpusで移管前後の出力差分を確認し、その後P2の証拠相関・校正とP3/P4の文脈・表層実現を進める。SURGE本体リポジトリはこの作業対象に含めない。


## 7. v0.46 conformance test update

- [x] Immutable Pack snapshot identity is checked.
- [x] Repeated decode of identical input is checked for deterministic best-path surfaces.
- [x] Input code-point budget rejection is checked without accepting a truncated prefix.
- [x] Invalid UTF-8 input rejection is checked.
- [x] Two-chunk streaming output is checked against one-shot decode output.
- [x] Two streaming sessions are checked for reading-state isolation.
- [ ] Japanese dictionary match ordering, unknown-kana fallback policy, lattice edge/candidate ceilings and expansion-budget behavior still require dedicated fixture-level assertions.
- [ ] The expanded suite remains unverified until GitHub Actions completes.


## 8. v0.47 Japanese Language Set conformance

- [x] Synthetic lexicon fixture verifies stable candidate cost ordering.
- [x] Dictionary match suppresses kana fallback at the same start position.
- [x] Unknown kana receives an explicit one-codepoint fallback candidate.
- [x] Candidate ceiling exhaustion is reported through the resource-limit flag.
- [x] Lattice edge ceiling exhaustion is reported through the resource-limit flag.
- [x] Morphology expansion exhaustion rejects the decode rather than returning a partial best path.
- [ ] Public host headers still expose Japanese implementation types; the provider-neutral type/API split remains the next architecture task.
- [ ] CI and golden-corpus parity remain unverified.


## 9. v0.48 provider-neutral streaming boundary

- [x] Generic `StreamingSession` no longer accepts Japanese `LexiconTrie`, scorer or connection-model types.
- [x] The Language Pack computes candidate paths and passes completed paths into `append_decoded`.
- [x] The generic session owns only reading state, revision, bounded acceptance and commit behavior.
- [x] Rejected over-limit chunks preserve the prior reading, snapshot and revision.
- [ ] Remove the remaining Japanese decoder/resource declarations from the generic public `engine.hpp` header.
- [ ] Verify all static/shared, C API, Python and streaming tests in CI.


## 10. v0.49 provider-neutral decode policy

- [x] Language Pack accepts generic path/input/lattice/expansion budgets through `LanguageDecodeOptions`.
- [x] Japanese Language Pack translates the neutral budget into its private decoder options.
- [x] C API calls the neutral contract; its public C ABI is unchanged.
- [x] Remove the former `DecoderOptions` overload from the Language Pack interface.
- [x] Add conformance checks for path count, input ceilings, invalid UTF-8 and transactional per-decode streaming rejection.
- [ ] CI build/test for the latest head.
- [ ] Remove remaining Japanese implementation declarations from generic public headers.
- [ ] Complete production renderer, production dictionary, held-out quality evaluation and target-PC resource qualification.


## 11. v0.50 Japanese adapter ownership

- [x] Move Japanese `LanguageAdapter` implementation and numeric parsing/rendering into `language_sets/ja-JP/surge_language.cpp`.
- [x] Move Japanese SURGE Cluster sidecar loading/scoring into `language_sets/ja-JP/surge_cluster.cpp`.
- [x] Compile both sources through the shared PIC-enabled Japanese Language Set OBJECT target.
- [x] Remove the moved source files from generic host static/shared source lists.
- [ ] Verify static/shared build and all CTest/Python integration targets in CI.
- [ ] Move the remaining Japanese implementation declarations out of the generic public headers.
- [ ] Complete production dictionary, full surface renderer and held-out/target-device qualification.


## 12. v0.51 Japanese API separation and surface renderer

- [x] Move Japanese scorer, correction, lattice, connection, trie, morphology and decoder API declarations to `japanese_decoder.hpp`.
- [x] Remove Japanese decoder API declarations and policy enums from generic `engine.hpp`.
- [x] Move Japanese LanguageAdapter and numeric helper declarations to `japanese_language.hpp`.
- [x] Change generic LanguageAdapter hypothesis creation to accept generic Candidate and an explicit provider-neutral score.
- [x] Add provider-neutral `SurfaceRenderOptions` and `LanguageAdapter::render_path`.
- [x] Implement bounded Japanese path rendering with source-span validation, opt-in numeric normalization, optional Latin/Japanese spacing and optional terminal punctuation.
- [x] Route SURGE-selected token surfaces through LanguageAdapter after UnifiedDecision.
- [x] Add Japanese renderer unit and SURGE runtime integration tests.
- [x] Add Python conformance test that prevents Japanese decoder declarations from leaking into generic public headers; register it with CTest.
- [ ] CI run #970 or a newer run for the final PR head, including static/shared/Python integration.
- [ ] Complete inflection/okurigana and date/time/unit/currency extraction.
- [ ] Production dictionary, held-out accuracy and target-PC resource qualification.


## 13. v0.52 dictionary-backed inflection and okurigana

- [x] Preserve Sudachi V1 POS5 as conjugation type and POS6 as conjugation form.
- [x] Support explicit conjugation/inflection header aliases for compatible CSV sources.
- [x] Normalize `*` conjugation metadata sentinels to empty values.
- [x] Add dictionary-backed `回っ` and `食べて` decode/render regression cases; preserve exact dictionary surface and okurigana.
- [x] Add multi-token `回っ + て → 回って` and `食べて + いる → 食べている` decoder/renderer regression cases.
- [x] Keep renderer non-generative: do not guess inflection from a lemma or alter the selected surface.
- [ ] Run final-head CI and all static/shared/C/Python tests.
- [ ] Measure inflected-form Candidate Recall@N, held-out CER and sentence exact match using a licensed production dictionary.
- [ ] Add auxiliary/particle/contraction chains and measure their recall and streaming stability.


## 14. v0.53 SURGE output persistence compatibility

- [x] Remove the unsupported `SemanticDelta::journal_events` member access that blocked static/shared compilation.
- [x] Persist normalization proposal/commit as typed Evidence nodes in the same SemanticDelta transaction as Claims/Evidence.
- [x] Attach output Evidence to a deterministic `NORMALIZATION_OUTPUT` cluster.
- [ ] Verify build, test, journal recovery and idempotent replay on the final PR head.


## 15. v0.54 Japanese inflection-chain conformance

- [x] Verify `回っ` plus connective particle `て` renders as `回って`.
- [x] Verify `食べて` plus auxiliary `いる` renders as `食べている`.
- [x] Verify the selected path covers the full reading with contiguous spans.
- [ ] Measure inflection-chain Candidate Recall@N and streaming stability on held-out licensed dictionary data.


## 16. v0.55 Release test reliability

- [x] Keep C/C++ test assertions enabled in Release builds.
- [x] Migrate generic streaming tests to the provider-neutral decoded-path API.
- [x] Propagate exact Japanese Pack loader diagnostics.
- [x] Fix the Sudachi V1 TSV test delimiter.
- [ ] Re-run final-head CI with assertions active and resolve all failures.


## 17. v0.57 typed Japanese entities and SURGE graph integration

- [x] Generalize `LanguageAdapter::make_numeric_entity` into provider-neutral `make_entity` and `EntityTarget`, retaining the numeric forwarding alias.
- [x] Parse bounded Japanese Date, Time (hour/minute/second), Range and explicit Identifier entities.
- [x] Render full-reading Date/Time/Range after path coverage validation when structured numeric normalization is enabled.
- [x] Persist full-reading Date/Time/Range as SURGE Entity + Evidence nodes with Supports/About relations, temporal validity and bounded entity clustering.
- [x] Verify graph metrics and journal recovery for typed date entities.
- [x] Verify static/shared build, all 29 CTest cases, microbenchmarks, reference Pack and Python policy tests on CI #1042.
- [ ] Extend typed entity extraction from full-reading to bounded partial spans inside longer sentences.
- [ ] Add calibrated entity evidence to UnifiedDecision before candidate selection; validate with held-out CER and Candidate Recall@N.
- [ ] Complete locale-specific calendar, relative-date and timezone policies; automatic identifier recognition remains intentionally disabled.


## 18. v0.58 partial-span entities and pre-decision evidence

- [x] Add provider-neutral bounded `EntitySpan` extraction contract with a default empty implementation.
- [x] Implement Japanese partial-span Date, Time and Range extraction with 128-code-point per-span and 16-entity limits.
- [x] Render a typed span inside longer text only when token boundaries exactly cover the span; preserve surrounding selected surfaces.
- [x] Add span-aligned `typed_entity` evidence before SURGE UnifiedDecision when candidate surfaces match the normalized entity representation.
- [x] Add metrics for extracted entities, evidence observations, selected evidence and persisted entities.
- [x] Persist each extracted Entity/Evidence pair in ContextGraph and relate only Claims contained within its span.
- [x] Add adapter tests for multiple partial entities and sentence rendering.
- [x] Add runtime test proving typed entity evidence changes the pre-decision selection and persists two entities.
- [x] Verify latest branch head with complete static/shared CI #1064; all 29 CTest cases, bounded microbenchmarks, Japanese reference Pack and Python policy tests passed.
- [ ] Calibrate `typed_entity` family weight on a held-out development corpus; evaluate CER and Candidate Recall@N on a separate fixed test corpus.
- [ ] Extend typed extraction to context-bound units/currency, relative dates and locale-specific calendar policies only with explicit grammar and ambiguity tests.


## 18. v0.59 typed-entity evidence calibration preparation

- [x] Add typed_entity to the recognized SURGE evidence-family policy grid.
- [x] Include disabled (0.0), conservative (0.5), default (1.0) and strong (1.5) typed-entity policy alternatives.
- [x] Keep calibration strictly on the dev split and retain the existing 1,792 MiB fail-closed RSS gate.
- [x] Extend the deterministic calibration unit test to verify the typed-entity family and selected policy output.
- [x] Add the typed-entity family to the baseline policy fixture.
- [ ] Run PR CI and the pinned Sudachi Small + UD Japanese-GSD workflow to obtain actual dev calibration and held-out test results.
- [ ] Compare typed-entity disabled/default/calibrated policies on the same frozen test split; report CER, sentence exact, override/defer counts, runtime, and Peak RSS.
- [x] Add typed-entity evidence count metrics to the real-data evaluator report and calibration candidate rows before expanding entity grammar coverage.
- [x] Bind the ja-JP LanguageAdapter in the real-data SURGE evaluator so typed extraction is actually active during dev/test evaluation.

This stage only makes the v0.58 typed-entity evidence weight calibratable. No preferred weight or accuracy gain is claimed until the pinned real-data workflow completes.


## 19. v0.60 N-best candidate generation diagnostics

- [x] Report raw decoder surface exact Candidate Recall@1, @5 and @10 over the full evaluation set.
- [x] Report best-candidate oracle CER@5 and @10 to separate candidate generation coverage from SURGE path selection and rendering.
- [x] Report conditional SURGE exact success given gold exact coverage in the top-10 raw candidates.
- [x] Report mean N-best paths per case; decoder failures count as misses in Recall@N and as full-reference deletion distance in oracle CER.
- [x] Propagate candidate-generation metrics into each dev calibration candidate row without using test data for selection.
- [x] Add a black-box CTest contract asserting recall monotonicity and oracle-CER monotonicity.
- [ ] Run latest-head CI and verify candidate metrics on the pinned Sudachi Small + UD Japanese-GSD corpus.
- [x] Classify cases into missing candidate, candidate-available runtime failure/defer/exact/wrong output, and wrong output matching vs outside raw N-best.
- [x] Enforce classification partition identities in the real-evaluator CTest and propagate the report into dev calibration rows without using it for policy selection.
- [ ] Use pinned held-out results to prioritize dictionary/search, SURGE selection, or renderer work; do not infer token-level recall from sentence-level exact coverage.

Candidate Recall@N here is sentence-level exact surface coverage over the raw concatenated N-best paths. It is a diagnostic upper bound for decoder candidate coverage, not a token-aligned morphological recall metric and not a claim of normalized-output accuracy.


## 20. v0.61 candidate error attribution

- [x] Add a sentence-level error classification report for missing top-10 candidate coverage, runtime failure, defer, exact SURGE output, and committed-but-wrong output.
- [x] Split committed wrong outputs into outputs matching another raw N-best surface and outputs outside the raw N-best set; treat the latter as a diagnostic signal, not automatic proof of renderer error.
- [x] Assert complete partition identities and Recall@10 consistency in the black-box evaluator test.
- [x] Propagate error-classification metrics into each dev calibration row for diagnosis only; policy selection remains CER/exact based.
- [ ] Verify final-head CI and inspect classification on the pinned Sudachi Small + UD Japanese-GSD corpus.
- [ ] Use real held-out diagnostics to select the next optimization target; retain the limitation that coverage is sentence-level, not token-aligned morphological recall.


## 21. v0.62 gold-token-aligned Candidate Recall

- [x] Retain trusted gold token reading/surface pairs from the six-column document/sentence corpus format.
- [x] Match gold tokens against decoder path tokens by exact Unicode code-point span and exact surface.
- [x] Report token-weighted Recall@1/@5/@10 separately from sentence-level exact surface coverage.
- [x] Report null token recall for sentence-only corpora instead of inferring token boundaries.
- [x] Propagate token-aligned metrics into dev calibration rows for diagnosis only; do not use them for policy selection.
- [x] Add a six-column token-aligned smoke fixture and monotonicity contract test.
- [ ] Verify final-head CI and inspect token-level coverage on the pinned Sudachi Small + UD Japanese-GSD corpus.
- [ ] Use token-level misses to separate dictionary absence from segmentation-boundary mismatch before changing decoder search budgets.


## 22. v0.63 token candidate miss attribution

- [x] Classify each gold token against the top-10 decoder paths by exact span+surface, same span with wrong surface, same surface at another span, or no matching surface.
- [x] Make the four classes mutually exclusive and exhaustive, including decoder path failures.
- [x] Verify attribution totals equal the gold-token denominator and exact matches agree with token Recall@10.
- [x] Propagate the attribution object into dev calibration rows for diagnosis only; keep policy selection independent.
- [x] Document that POS-specific attribution is not yet supported because the current gold corpus does not contain trusted POS tags.
- [ ] Run latest-head CI and inspect attribution on the pinned Sudachi Small + UD Japanese-GSD development corpus.
- [ ] Prioritize lexicon, segmentation, inflection metadata or SURGE selection only after real-data class distribution is available.


## 23. v0.64 pinned real-corpus SURGE runtime evaluation

- [x] Extend the pinned Sudachi Small workflow to build the current `kana_decoder_surge_eval` target.
- [x] Generate a combined source-lock artifact containing the Sudachi dictionary lock, UD Japanese-GSD lock, upstream corpus manifest and document-disjoint split manifest.
- [x] Calibrate the eight SURGE evidence-policy presets on dev only using the current SURGE Runtime evaluator and train-only token sequences.
- [x] Apply the selected dev policy once to the frozen test split; do not feed test metrics back into calibration.
- [x] Preserve sentence-level Candidate Recall, oracle CER, token-aligned Recall, token miss attribution, candidate error partitions, typed-entity evidence counters, elapsed time and Peak RSS in machine-readable reports.
- [x] Enforce the 1,792 MiB peak-RSS ceiling for every dev calibration subprocess and the held-out test evaluator.
- [x] Upload aggregate evaluation reports and provenance only; do not upload raw corpus or sentence traces.
- [ ] Complete latest-head C++/Python CI after the calibration-fixture JSON syntax correction.
- [ ] Complete the new pinned real-corpus SURGE Runtime workflow on this branch.
- [ ] Inspect dev token-miss class distributions and select the next optimization target without consulting test results.
- [ ] Publish held-out CER, sentence exact, candidate recall, miss attribution and resource metrics only after the new workflow succeeds. Hosted CI measurements are not target-PC performance claims.

The previous successful Sudachi workflow on commit `84ef743` evaluated the legacy sequential/cluster path. Its metrics remain a separate historical baseline and are not presented as v0.64 SURGE Runtime results.
