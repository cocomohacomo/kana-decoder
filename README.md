# kana-decoder

純かなテキストを入力として、言語別Language Packとストリーミング文脈推定に基づき、漢字・かな・カナ・アルファベット等、数字・記号・句読点を含む正文へ変換する軽量デコーダです。辞書・形態素体系・正書法モデルは言語ごとに換装可能です。

## 実装状況
- C++17 CoreとC API
- Context-masked n-gram候補順位付け（未校正スコアは確率として扱わない）
- Language Pack TSVからの辞書Trie構築と候補ラティス自動生成
- n-gram TSVと品詞接続行列（connections.tsv）を用いる形態素候補ラティス統合N-bestデコーダ
- bounded Streaming Session（追加入力ごとの再デコード、revision付き暫定結果、共通接頭辞の部分commit、best-path commit）
- 低確信時のAmbiguity Segmentと [雨/飴/other] 表示
- Conservativeモードの原文かなフォールバック
- 原文を保持する軽度誤り補正仮説
- 数字・URL等の保護、補正候補数の上限
- UTF-8不正シーケンスの拒否とコードポイント単位の入力範囲管理
- C ABIからのLanguage Packロード・かなデコード
- Morphological / Semantic / Orthographic依存グラフ
- 正規化TSVからのLanguage Pack生成、候補ID整合性検査、コーパス由来n-gram学習
- 完全一致率・Unicode code point CER・decode failureを測る評価CLI
- オフラインPack Manifest・SHA-256・ライセンス・展開サイズ・symlink・未登録ファイル検証
- CTest + Python unittest
- GitHub Actionsによるpush / PR時のC/C++ビルド・統合テスト・品質スモーク評価・Pack生成テスト・ベンチマーク・Pack検証

## ビルドとテスト
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    ctest --test-dir build --output-on-failure
    python3 tools/verify_pack.py packs/ja-lite

## Language Pack
- 日本語Lite参照Pack: packs/ja-lite/manifest.json
- C++ API `load_language_pack` はManifestをnative検証した後、lexicon.tsv / ngram.tsv / optional connections.tsv / correction_rules.tsvを一括ロードする。辞書TSVは最大12列: `kana, surface, lemma, pos, reading_group, candidate_id, word_cost, connection_cost, left_id, right_id, inflection_type, inflection_form`。5列/10列形式も後方互換。
- 配布前は `python3 tools/verify_pack.py packs/ja-lite` で追加検証する。C++ローダーもSHA-256・サイズ・ABI・ライセンス・path・symlink・未登録ファイルを検証するが、現行native verifierはbuilderの限定Manifestスキーマに対応する。
- 接続行列を含む自作の小規模fixtureであり、実用語彙カバレッジや統計的な品詞接続精度を持つ辞書ではありません。
- 外部辞書はライセンス確認前のため含めていません。
- 標準軽量Pack目標は展開後30 MiB以下。

## Japanese dictionary adapters and correction-aware decoding
- Sudachi 17-column CSV: `python3 tools/import_japanese_dictionary.py --input source.csv --format sudachi-csv --output normalized.tsv`
- IPADIC-compatible 13-column CSV: `--format ipadic-csv`
- Explicit header mapping for other exports, including audited UniDic conversions: `--format header-csv`
- Import preserves source POS labels, inflection/conjugation type and form, word cost, and left/right connection IDs. Connection matrices are supplied separately as normalized `connections.tsv`.
- Runtime correction integration: `decode_with_corrections` scores bounded typo hypotheses through the morphology decoder and applies only candidates meeting configured improvement thresholds. Digits, URL-like strings, and protected spans are not auto-corrected.
- Native Pack loading verifies Manifest ABI/format, license metadata, payload SHA-256 and size, relative paths, symlinks and unlisted files before reading lexicon/ngram data. Optional correction_rules.tsv is loaded transactionally with a 256-rule ceiling. Runtime ceiling: 30 MiB expanded payload; Manifest file ceiling: 1 MiB; payload count ceiling: 64.
- Evaluation TSV accepts an optional third domain column and reports overall plus per-domain Exact Match / CER.

## Language Pack import / quality evaluation
- 仕様と入力形式: docs/PACK_IMPORT_AND_EVALUATION.md
- 正規化辞書TSVとライセンスファイルからPack生成: `python3 tools/build_language_pack.py --help`
- 学習用のtoken candidate ID列からU/L/R n-gram countを生成。学習IDは辞書候補IDに存在する必要があります。
- 評価: `./build/kana_decoder_eval packs/ja-lite tests/data/ja-smoke-gold.tsv`
- smoke corpusは配線確認用で、実用精度の代表値ではありません。

## 設計仕様
- 仕様書 v0.7: docs/SPECIFICATION_v0.7.md
- 仕様書 v0.6: docs/SPECIFICATION_v0.6.md
- 仕様書 v0.5: docs/SPECIFICATION_v0.5.md
- 仕様書 v0.4: docs/SPECIFICATION_v0.4.md
- 仕様書 v0.3: docs/SPECIFICATION_v0.3.md
- 仕様書 v0.2: docs/SPECIFICATION_v0.2.md
- 仕様書 v0.1: docs/SPECIFICATION_v0.1.md
- Streaming Context Architecture: docs/STREAMING_CONTEXT_ARCHITECTURE.md
- 実装・検証報告: docs/IMPLEMENTATION_REPORT_v0.1.md

## ライセンス
Coreと自作fixtureはMIT。将来追加するLanguage Packは個別にライセンス・upstream・checksumを記録します。
