# Streaming Context Architecture

kana-decoderの文脈エンジンは、SURGEのStreaming-Nativeな文脈認識思想を日本語デコーディング向けに縮約した独立コンポーネントである。

## 1. 認識単位

入力チャンクをイベント化し、読み範囲に対応する候補群を保持する。候補は確定した正解ではなく、文脈Evidenceによって順位が変わる仮説として扱う。

## 2. Context Cluster

話題・対象・ドメインごとの状態を保持する。複数Clusterを並行維持し、入力との関連度、再利用頻度、Evidence、QuestionからFocusを更新する。話題の切替後もClusterを即時削除せず、再活性化を許容する。

## 3. Claim / Evidence

Claimは「この読み候補は特定の語義である」「現在の話題は特定の領域である」等の仮説。Evidenceは入力イベント、辞書、Hotword、形態素接続、後続述語など。Evidenceは入力位置と有効範囲を参照し、矛盾や期限切れに応じて依存Claimを再評価する。

## 4. Question / Focus

候補間の差が小さい、または誤変換コストが高い場合にQuestionを生成する。Questionは対象読み範囲、候補、未解決Aspect、関連Cluster、重要度を持つ。FocusはQuestionと入力関連性によって関連Clusterへ向けられる。後続入力がEvidenceを追加するとQuestionを解消または部分解消する。

## 5. 再評価

新規チャンク到着時、末尾ラティスだけでなく、未確定範囲の依存関係をたどって再評価する。確定範囲は通常の更新対象から除外する。再評価結果はrevision付き差分として返す。

## 6. Forgetting

Context Cluster / Claim / Questionには上限を設ける。Focus、重要度、Evidence強度、再利用頻度、時間減衰から休眠化・要約・破棄を決める。確定済み文字列は忘却対象にしない。

## 7. 決定性

同一のイベント列、辞書世代、モデル版、設定から同一の状態遷移と出力を得る。候補上限時の切り捨て、同点解消、忘却順序も決定的にする。
