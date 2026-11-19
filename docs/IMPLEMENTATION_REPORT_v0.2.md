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
- [ ] CIの最新headでビルド・テスト成功を確認
- [ ] 大規模実データコーパスで比較評価を実行
- [ ] 性能・メモリ・精度ゲートを確定
- [ ] Language Set完全移管と2言語交換試験
