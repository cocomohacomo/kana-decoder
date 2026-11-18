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


## 8. Branch-based Candidate Management

かな漢字変換候補は、単一の最尤候補へ早期に縮退させず、複数の正文化仮説をBranchとして保持する。Branchは形態素候補列と入力範囲を持つ仮説であり、Context Clusterとは別の概念である。

### 8.1 Branch領域

- Main: 現時点の代表候補。原則1つ。
- Active: Mainに次ぐ上位候補。通常の再評価とFocusの対象。
- Archive: 現時点の上位集合から外れた候補。通常の展開対象から外すが、復帰に必要な要約とEvidence参照を保持。
- Committed: 確定済み出力。不変。
- Pruned: 重複・無効・保持期限切れ等で削除された候補。

Main選択は表示上の代表選択であり、曖昧性の解消や意味上の確定を意味しない。

### 8.2 イベント更新

各入力イベントでMainとActiveを増分展開し、新しいEvidenceを関連付け、未確定範囲を再スコアリングする。重複候補を統合し、決定的な順位規則でMainを選び、上限を超えた下位候補をArchiveへ移す。Archive全件を毎回再展開してはならない。

### 8.3 Archive復帰

後続入力、Cluster Focusの変化、Claimの有効性変化、HotwordUpdate、Main/Activeの整合性低下等が候補を支持した場合、Archive候補を再活性化候補として取り出し、現在有効なEvidenceで再評価する。Archive移動は候補の否定ではない。

### 8.4 Focus / Commit連携

FocusはMainと上位Activeが参照するCluster・Question・Evidenceへ重み付けする。Archiveは通常Focusから外す。Commit Controllerが範囲を確定したら、その範囲のMainをCommittedへ移し、通常更新で書き換えない。

### 8.5 計算量と決定性

Active数・Archive数・候補長・再活性化回数に上限を設ける。上限到達時はCommitted保護、形態素接続妥当性、Branchスコア、Evidence有効性、新しさ、安定IDの順に整理する。同一イベント列・辞書世代・モデル版・設定から同一状態遷移を得る。

候補の共通prefix/suffixは共有し、木またはDAGで表現して重複メモリを抑える。診断モードではMain/Active/Archiveの順位、状態遷移、Archive移動・復帰・削除理由を取得可能にする。

