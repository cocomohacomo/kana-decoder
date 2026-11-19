# kana-decoder 実用稼働ロードマップ v0.1

更新日: 2026-11-19
対象ブランチ: `refactor/surge-core-as-runtime`
対象仕様: `SPECIFICATION_v0.44.md`

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
- [ ] static/shared両方のLanguagePackテストをCIで実行する。
- [ ] C APIでPackロード失敗時にEngine状態が変化しないことを検証する。
- [ ] Engine破棄後もSessionがPack snapshotを安全に保持することを検証する。
- [ ] ストリームreset/journal再接続後もLanguageAdapterが再バインドされることを検証する。

### P1: 次のコード移管
- [ ] `MorphologyDecoder` とlattice探索を `language_sets/ja-JP/` に物理移動する。
- [ ] 日本語Language Setを独立したCMake targetとしてビルド可能にする。
- [ ] 共通データ型とprovider interfaceを公開ヘッダーへ整理し、Hostから日本語実装ヘッダーを参照させない。
- [ ] 辞書照合、未知語fallback、候補上限、beam/expansion上限をLanguage Set conformance testで固定する。
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

v0.44のimmutable LanguagePack境界を前提に、次のコード変更はP0のCI確認後、P1の形態素/lattice物理移管から着手する。SURGE本体リポジトリはこの作業対象に含めない。P1移管は既存golden出力との完全互換性を維持し、差分が出た場合は性能改善と混同せず個別に原因を特定する。
