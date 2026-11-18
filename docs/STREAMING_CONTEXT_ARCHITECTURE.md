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



## 9. Bounded-Work Execution

### 9.1 契約

通常のstream updateは、ストリーム総量・過去イベント数・Archive履歴長に依存しない固定予算で処理する。厳密なO(1)の対象はホットパスの状態更新である。入力転送・全文出力・辞書初期化・明示Batch処理はデータ量に比例するため別契約とする。

### 9.2 固定上限

初期上限候補は1イベント64 code points、未確定窓64 code points、Branch総数8、Active 4、Archive 16、Branchあたり形態素32、Cluster 16、Claim 32、ClaimあたりEvidence 4、Open Question 8、Focus 4、Hotword 128とする。これらは構成時に固定し、無制限拡張を許可しない。ベンチマーク後に数値を調整しても、全て有限上限でなければならない。

### 9.3 固定予算処理

各イベントはIngest、末尾窓のLattice更新、Branch展開、スコアリング、Context更新、Archive probe、Main選択、差分生成を行う。各フェーズに最大処理件数を設ける。上限に達した探索は打ち切るか固定長Pending Queueへ延期し、同一イベント内で無制限に続行しない。

### 9.4 データ構造

固定配列・リングバッファ・固定ノードプールを用いる。Branch pathは共有prefix/DAG、Contextは固定Cluster/Claim/Question配列、Evidenceは上位項目と集約統計、Focusは最大4件とする。辞書Trieは読み深さと各ノードの分岐数を制限する。ハッシュ探索は最大probe数を設定する。

### 9.5 Incremental Context / Forgetting

入力から直接影響を受けるCluster・Claim・Questionのみ更新する。全Cluster・全履歴走査を禁止する。忘却は固定件数を巡回するclock-hand方式で実行し、期限判定もスロット単位で遅延評価する。

### 9.6 Branch / Archive

MainとActiveのみを通常展開し、Archive全件を毎回走査しない。Archive再活性化検査はリングカーソルで最大K件/イベント。候補順位付けは固定長候補配列内で行う。Branch path長とノード数に上限を設け、超過時はCommit境界で窓を分割し、境界要約だけを引き継ぐ。

### 9.7 Pending Work

優先度付き固定長Queueで延期処理を管理し、イベントごとに最大M件だけ実行する。Queue満杯時は低優先度の作業を統合・破棄し、診断カウンタを増やす。Main整合性とCommitted不変性を最優先する。

### 9.8 長文・メモリ

固定長未確定窓をスライドさせ、Committed出力は呼び出し元の領域へ移す。全文連結はO(n)としてホットパス外に置く。Engine/Session生成時に固定容量を確保し、stream update中のヒープallocationを禁止する。

### 9.9 検証

長時間ストリーム、Branch/Cluster/Question/Hotword上限、Archive飽和、Pending Queue飽和、長文分割を含むテストを行う。event updateのp50/p95/p99/max、処理ステップ数、再評価数、Archive probe数、Queue深度、peak RSS、allocation countを測定する。入力総量が増えても1イベント当たり処理ステップ数が上限内で一定であることを受け入れ条件とする。

