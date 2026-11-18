# kana-decoder

純かなテキストを入力として、言語別Language Packとストリーミング文脈推定に基づき、漢字・かな・カナ・アルファベット等、数字・記号・句読点を含む正文へ変換する軽量デコーダです。辞書・形態素体系・正書法モデルは言語ごとに換装可能です。

## 実装状況
- C++17 CoreとC API
- Context-masked n-gram候補順位付け（未校正スコアは確率として扱わない）
- 形態素候補ラティス統合N-bestデコーダ（形態素コスト + 左文脈再順位付け、固定Beam/展開上限）
- 低確信時のAmbiguity Segmentと [雨/飴/other] 表示
- Conservativeモードの原文かなフォールバック
- 原文を保持する軽度誤り補正仮説
- 数字・URL等の保護、補正候補数の上限
- Morphological / Semantic / Orthographic依存グラフ
- オフラインPack Manifest・SHA-256・ライセンス・展開サイズ検証
- CTest + Python unittest
- GitHub Actionsによるpush / PR時のビルド・テスト

## ビルドとテスト
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    ctest --test-dir build --output-on-failure
    python3 tools/verify_pack.py packs/ja-lite

## Language Pack
- 日本語Lite参照Pack: packs/ja-lite/manifest.json
- 自作の小規模fixtureであり、実用語彙カバレッジを持つ辞書ではありません。
- 外部辞書はライセンス確認前のため含めていません。
- 標準軽量Pack目標は展開後30 MiB以下。

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
