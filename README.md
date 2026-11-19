# kana-decoder

純かなテキストを入力として、言語別Language Packとストリーミング文脈推定に基づき、漢字・かな・カナ・アルファベット、数字・記号・句読点を含む正文へ変換するCPU・完全オフライン対応デコーダです。

## 実装状況
- C++17 Core / C API、Trie辞書、形態素候補ラティス、N-bestデコード
- Context-masked n-gram候補順位付け、曖昧性保持、かなフォールバック
- 接続行列、品詞・活用情報を含む候補デコード
- C++ / C APIストリーミングセッション、暫定出力、共通接頭辞commit、best-path commit
- pip install可能なPython ctypesパッケージ、N-best構造化結果、ストリーミングAPI
- 出力バッファ不足時に入力・revision・commit状態を変更しないトランザクション動作
- 軽度誤り補正、UTF-8検証、Pack Manifest / SHA-256 / license検証（C++側は容量上限を共有しSHA-256をストリーミング検証）
- Sudachi V0/V1 import adapter、Pack builder、文書単位のtrain/dev/test分割
- GitHub ActionsによるC++ / Pythonテストとベンチマーク\n- 実コーパスを使ったC++全文デコード評価（予測左履歴、未来文脈なし）

## ビルドとテスト
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    ctest --test-dir build --output-on-failure
    python3 -m pip install .
    python3 -m unittest discover -s tests -p 'test_*.py' -v

## Language Packとサイズ方針
- 日本語Lite参照Pack: packs/ja-lite
- PackはManifest・SHA-256・ライセンス情報を持ち、ローカルで検証してロードします。
- 30 MiBは暫定的な最適化目標であり、開発・機能完成を妨げる制限ではありません。実サイズ・ロード時間・ピークRSSを測定し、必要な最適化は後段で行います。
- 外部辞書は対象リリースのライセンス・NOTICE確認と実データのハッシュ検証後にPack化します。Core MITは第三者データのライセンスを変更しません。

## Sudachi V1とコーパス検証
- source lock: licenses/sudachidict-small-20260723-source-lock.json
- source acquisition: python3 tools/fetch_sudachi_source.py --output-dir work/archives
- Pack build: python3 tools/build_sudachi_pack.py --small-zip work/archives/small_lex.zip --matrix-zip work/archives/matrix.def.zip --work-dir work/build --output dist/ja-jp-sudachi-small
- 文脈学習用データの準備: python3 tools/prepare_context_corpus.py --lexicon normalized-lexicon.tsv --corpus tokenized-corpus.tsv --output-dir work/context-data
- 実コーパス評価: tools/prepare_ud_gsd_corpus.py と tools/evaluate_context_ablation.py。UD Japanese-GSDのtrain/dev/testを固定commitからCI時のみ取得し、gold左右文脈によるoracle ablationと、各方式が独立した予測履歴を使うcausal sequential評価を分けて比較します。causal評価もgold token境界を使うため、全文デコード精度とは区別します。
- train_sequences.tsv をPack builderの --sequences に渡し、dev_gold.tsv / test_gold.tsv は学習に混ぜずに評価へ使用します。
- 入力コーパスは文書ID・文単位の形態素トークン列。文書単位で分割し、同一文書がtrain/dev/testをまたがないようにします。辞書にない reading/surface は黙って捨てずにエラーにします。
- GitHub Actionsからの手動Pack生成: Actions → Build Sudachi Small Pack → Run workflow（生成Packは30日間Artifactとして保存）
- V1 header matching is case-insensitive and ignores underscores.
- train/dev/test leakage guard: tools/validate_corpus_splits.py
- 詳細: docs/PACK_IMPORT_AND_EVALUATION.md

## 実行時メモリとSURGE診断
- v0.34のクラスター評価は距離別・方向付き証拠を使用。SURGE Questionは異なる表層候補が2つ以上ある場合だけ計上し、単一候補を棄権に含めません。
- v0.26で入力長・ラティス・候補・展開状態・trace量の決定的上限を導入。上限超過は不完全な候補を返さずfail-closed。
- Sudachi実PackのCIは1,792 MiBのプロセスメモリ上限とPeak RSS検査を実施。
- v0.27でunigram priorと隣接形態素のmasked n-gram証拠を分離。簡易SURGE probeは差別化された文脈証拠がある場合のみQuestionを解決し、証拠不足時はFocusへ残します。
- tools/evaluate_surge.py は候補cluster・矛盾Question・Focus・gold候補coverageを測定する非変更型の簡易SURGE probeです。
- tools/evaluate_ablation.py は全読みを覆う単一span候補に限定し、形態素スコア・combined local score・SURGEの根拠付き解決を比較します。複数形態素文の正解token alignmentを推測しません。

## 仕様
- v0.39: docs/SPECIFICATION_v0.39.md
- v0.36: docs/SPECIFICATION_v0.36.md
- v0.34: docs/SPECIFICATION_v0.34.md
- v0.34: docs/SPECIFICATION_v0.34.md
- v0.33: docs/SPECIFICATION_v0.33.md
- v0.32: docs/SPECIFICATION_v0.32.md
- Python API: docs/PYTHON_API.md
- v0.32: docs/SPECIFICATION_v0.31.md（v0.32校正入力堅牢化を含む）\n- v0.31: docs/SPECIFICATION_v0.31.md\n- v0.30: docs/SPECIFICATION_v0.30.md\n- v0.29: docs/SPECIFICATION_v0.29.md
- v0.28: docs/SPECIFICATION_v0.28.md
- v0.27: docs/SPECIFICATION_v0.27.md
- v0.26: docs/SPECIFICATION_v0.26.md
- v0.15: docs/SPECIFICATION_v0.15.md
- v0.14: docs/SPECIFICATION_v0.14.md
- v0.13: docs/SPECIFICATION_v0.13.md
- v0.12: docs/SPECIFICATION_v0.12.md
- v0.11: docs/SPECIFICATION_v0.11.md
- v0.10: docs/SPECIFICATION_v0.10.md
- v0.9以前: docs/
- Licensing: docs/LICENSING_POLICY.md、docs/THIRD_PARTY_COMPONENTS.md


- v0.35: SURGEクラスタsidecar serializerを追加。train-only cluster / distance-conditioned evidence / dev thresholdをバージョン付きTSVとSHA-256 manifestへ出力。
- v0.36: C++ sidecar loader/scorerを追加し、SURGE Cluster単独・N-gram併用の全文評価を実装。既定無効・Pack非同梱の実験機能。
- v0.37-v0.38: SURGE経験記憶へword Unigram / Bigram / Trigram / 4-gram / Skip-Bigramを統合し、Beamごとの履歴を最大3候補へ拡張。
- v0.39: POS N-gramを別Evidenceとして統合。POS索引は評価時のみ明示構築し、通常の辞書ロードでは追加メモリを使わない。
- v0.42: [SURGE Core統合](docs/SPECIFICATION_v0.42.md)。独立SURGE Core v0.9.11をMITライセンスでvendoringし、EventJournal / ContextGraph / DependencyEngine / QuestionRouter / ChaosClusterLedgerを正文化の実行経路へ接続。N-best候補をEntity / Claimとして登録し、辞書語彙・接続遷移・Word N-gram・POS N-gram・SURGE Clusterを独立Evidenceとして記録。矛盾からQuestion・Interest・Focusを生成。未解決候補はChaos PatternHypothesisへ保持。時間的有効性、再評価、StreamingのProposal / Prefix Commit / Final CommitをSURGE Journalへ記録。C/Python APIから永続Journalを指定可能。  
- v0.41: [SURGE正文化基盤](docs/SPECIFICATION_v0.41.md)。固定容量Hypothesis / CandidateSet / SemanticFrame / SyntaxRelation / OrthographicHypothesis / Entity / Constraint / Decision / Revision / CommitSpanを追加。Evidenceの出所・信頼度・有効期間・依存関係と重複抑制集約、最大8経路の任意N-best再ランキング、日本語数詞の整数/小数解析、数値・日付・時刻・単位・通貨・範囲の型付き表記生成、句読点ポリシーを実装。既存Decoder既定動作とPack互換性は維持。
- v0.40: [統合校正仕様](docs/SPECIFICATION_v0.40.md)。Word/POS N-gramとSURGE Clusterを同じdev splitで144構成校正し、最良方式をtestで凍結評価。


## SURGE Language / Normalization Runtime migration

- v0.43: [SURGE Language and Normalization Runtime Migration](docs/SPECIFICATION_v0.43.md). Language Adapter and Normalization Adapter are both owned by SURGE. Language-specific recognition and rendering resources live in replaceable SURGE Language Sets; kana-decoder remains a thin input/output, compatibility and evaluation host.
- Framework contract: [SURGE Language Runtime and Replaceable Language Packs v0.1](https://github.com/cocomohacomo/surge/blob/feat/language-runtime-core/docs/surge/LANGUAGE_RUNTIME_AND_REPLACEABLE_LANGUAGE_PACKS_v0.1.md).
