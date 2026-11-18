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
- Sudachi V0/V1 import adapter、Pack builder、コーパス分割漏洩検証
- GitHub ActionsによるC++ / Pythonテストとベンチマーク

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
- GitHub Actionsからの手動Pack生成: Actions → Build Sudachi Small Pack → Run workflow（生成Packは30日間Artifactとして保存）
- V1 header matching is case-insensitive and ignores underscores.
- train/dev/test leakage guard: tools/validate_corpus_splits.py
- 詳細: docs/PACK_IMPORT_AND_EVALUATION.md

## 仕様
- Python API: docs/PYTHON_API.md
- v0.15: docs/SPECIFICATION_v0.15.md
- v0.14: docs/SPECIFICATION_v0.14.md
- v0.13: docs/SPECIFICATION_v0.13.md
- v0.12: docs/SPECIFICATION_v0.12.md
- v0.11: docs/SPECIFICATION_v0.11.md
- v0.10: docs/SPECIFICATION_v0.10.md
- v0.9以前: docs/
- Licensing: docs/LICENSING_POLICY.md、docs/THIRD_PARTY_COMPONENTS.md
