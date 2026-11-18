# kana-decoder

日本語かな漢字変換デコーダ。ひらがな文章を、ストリーミング文脈推定に基づいて漢字・かな表記と句読点を持つ文章へ変換する軽量エンジンです。

## 特徴

- Streaming-Native: 入力を逐次処理し、文脈状態を継続更新
- Context Cluster / Question / Focusによる話題・曖昧性管理
- Main / Active / ArchiveによるBranch型の複数正文化候補管理
- 後続EvidenceによるArchive候補の再活性化
- 後続入力による未確定候補の遡及的再評価
- 同音異義語の文脈推定補正
- 実行時ホットワード注入
- 暫定出力・確定出力・差分更新
- 句読点・表記正規化
- CPUのみ・完全オフライン
- C++コア + C API
- 1文節あたり1 ms未満を性能目標とする（実測検証が必要）

## 設計仕様

- [仕様書 v0.3](docs/SPECIFICATION_v0.3.md)
- [仕様書 v0.2](docs/SPECIFICATION_v0.2.md)
- [仕様書 v0.1](docs/SPECIFICATION_v0.1.md)

## 辞書方針

SudachiDictを第一候補としてライセンス監査を行い、UniDic等を補完候補として評価します。辞書の語彙データ、接続行列、文字定義などはファイル単位で由来とライセンスを確認し、未確認データは配布物に含めません。

## 状態

基本設計段階。辞書インポーター、ストリーミングContext Core、C APIの順に実装を進めます。
