# SURGE Core 実データ評価準備レポート v0.2

日付: 2026-11-19  
対象ブランチ: `refactor/surge-core-as-runtime`  
対象仕様: `SPECIFICATION_v0.43.md`

## 1. 今回実装した範囲

### SURGE Core

- `include/surge/decision.hpp` / `src/decision.cpp` を追加。
- `RecognitionDecision` を実装し、候補群の最終選択をCoreの責務として実行可能にした。
- `NormalizationDecision` を実装し、語彙・意味解釈の選択と表記候補の選択を別の決定として扱う。
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
