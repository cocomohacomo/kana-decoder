# kana-decoder

純かなテキストを入力として、言語別Language Packとストリーミング文脈推定に基づき、漢字・かな・カナ・アルファベット等、数字・記号・句読点を含む正文へ変換する軽量デコーダです。辞書・形態素体系・正書法モデルは言語ごとに換装可能です。

## 特徴

- Streaming-Native: 入力を逐次処理し、文脈状態を継続更新
- Context Cluster / Question / Focusによる話題・曖昧性管理
- Main / Active / ArchiveによるBranch型の複数正文化候補管理
- 後続EvidenceによるArchive候補の再活性化
- 固定容量・固定処理予算によるイベント当たり計算量の上限制御
- 長文ストリームでも一定サイズの未確定窓とSessionメモリを維持
- 後続入力による未確定候補の遡及的再評価
- 同音異義語の文脈推定補正と、低確信時の `[雨/飴/other]` 形式の曖昧性保持出力
- n-gram等の前後文脈推定による候補順位付け
- 原文を保持した軽度の言い間違い・文字起こし誤り補正（自動修正は高確信時のみ）
- 実行時ホットワード注入
- 暫定出力・確定出力・差分更新
- 漢字・かな・カナ・アルファベット等を含む多文字体系の正文化
- 数字・単位・記号・句読点の意味を保った正規化
- 言語別Language Packによる辞書・形態素体系・正書法の換装
- 句読点・表記正規化
- CPUのみ・完全オフライン
- C++コア + C API
- 1文節あたり1 ms未満を性能目標とする（実測検証が必要）

## 設計仕様

- [仕様書 v0.7](docs/SPECIFICATION_v0.7.md)
- [仕様書 v0.6](docs/SPECIFICATION_v0.6.md)\n- [仕様書 v0.5](docs/SPECIFICATION_v0.5.md)
- [仕様書 v0.4](docs/SPECIFICATION_v0.4.md)
- [仕様書 v0.3](docs/SPECIFICATION_v0.3.md)
- [仕様書 v0.2](docs/SPECIFICATION_v0.2.md)
- [仕様書 v0.1](docs/SPECIFICATION_v0.1.md)

## 言語パック・辞書方針

辞書モデルは言語ごとのLanguage Packとして換装可能にします。日本語Packの初期候補はSudachiDictとし、UniDic等を補完候補として評価します。辞書・接続行列・文字定義・モデルはファイル単位で由来とライセンスを確認し、未確認データは配布物に含めません。CoreとLanguage Packのライセンスは分離管理します。

## 配布サイズ目標（v0.6）

- Core runtime: 5 MB以下
- Lightweight Language Pack: 30 MB以下
- Core + 1 Lightweight Pack: 35 MB以下
- 専門語彙・高精度Packは任意拡張として分離
- サイズは展開後を基準とし、実測で検証する目標値

Coreは言語非依存とし、利用者は必要なLanguage Packだけを導入します。辞書・モデルはPack単位でバージョン、checksum、ライセンス、資源上限を管理します。

## 状態

基本設計段階。v0.7で曖昧性保持出力と軽度誤り補正を追加。v0.6でCoreと言語パックの分離配布、および軽量Pack（展開後30 MB以下）を標準目標として定義。辞書インポーター、Pack ABI、形態素デコーダ、Context Core、C APIの順に実装・検証を進めます。
