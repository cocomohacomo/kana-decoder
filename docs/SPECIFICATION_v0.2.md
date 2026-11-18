# kana-decoder 仕様書 v0.2

- Status: Design specification
- Version: 0.2.0
- Core: C++ / public ABI: C API
- Runtime: CPU only / fully offline
- Primary latency target: 1 ms未満 / 文節（定常状態）
- Core characteristic: Streaming-Native Contextual Kana-Kanji Decoding

## 1. 目的と位置付け

kana-decoderは、ひらがなを中心とする日本語の読みストリームを、継続的な文脈認識に基づいて漢字・かな表記、語境界、句読点を持つ文章へ変換する軽量デコーダである。

本プロジェクトの中心的特徴は、単発の入力文字列を独立に変換するのではなく、入力の到着に伴って文脈状態を更新し、既出の候補・暫定変換結果を新しい情報に基づいて再評価することにある。

SURGEのStreaming-Nativeな文脈認識思想を日本語かな漢字変換に特化して適用する。ただし、kana-decoderはSURGEの実行依存を持たない独立ライブラリとし、必要な概念のみを小さな専用Context Coreとして実装する。

## 2. 基本設計原則

1. **Streaming-Native**: 入力をチャンクまたはイベントとして逐次処理し、ストリーム状態を維持する。
2. **文脈優先の候補選択**: 読みだけでなく、話題、対象、述語、係り受けの手掛かり、直近の確定表現を評価する。
3. **遡及的再評価**: 後続入力で解釈が変わった場合、未確定範囲内の過去候補を再評価する。
4. **確定性の管理**: 暫定出力と確定出力を区別し、確定済みテキストを不用意に変更しない。
5. **疑問・関心モデル**: 解消していない曖昧性をQuestionとして保持し、関連するContext ClusterへFocusを向ける。
6. **ホットワードは証拠の一つ**: 強制置換ではなく、文脈・接続・語彙情報と統合して評価する。
7. **軽量・決定的**: CPUのみで動作し、同一入力・同一状態・同一モデル版から再現可能な結果を得る。
8. **不確実性の尊重**: 根拠が弱い場合は、かな表記や複数候補を保持し、誤った漢字を断定しない。
9. **辞書非依存のコア**: 辞書・形態素体系・スコアラーを差し替え可能にする。

## 3. 入出力とストリームモデル

### 3.1 入力イベント

入力はUTF-8テキストの連続として受け付け、内部では次のイベントに正規化する。

- TextChunk: 読みテキストの追加
- BoundaryHint: 文節・節・文境界のヒント
- ContextHint: 話題、ドメイン、話者、対象などの任意ヒント
- HotwordUpdate: ホットワードの追加・更新・削除
- CommitRequest: 指定位置までの出力確定要求
- Reset: セッション文脈の初期化

イベントにはstream_id、sequence_no、入力オフセット、任意のtimestampを持たせる。時刻がない場合はsequence_noを順序の基準とする。

### 3.2 チャンク境界

チャンク境界は文節境界とは限らない。UTF-8文字境界・読み単位境界を安全に扱い、未完の文字列や活用語尾が次チャンクに続く可能性を保持する。チャンクごとに独立変換してはならない。

### 3.3 出力

出力は次を持つ。

- provisional_text: 再評価可能な暫定出力
- committed_text: 確定済み出力
- revision: 変更世代番号
- segments: 読み範囲・表記範囲・候補・スコア情報
- punctuation_events: 句読点の挿入・撤回イベント
- unresolved_questions: 未解決曖昧性の要約（診断モード）

出力更新は差分として取得可能とする。差分には対象範囲、旧表記、新表記、revisionを含める。

## 4. 全体アーキテクチャ

1. UTF-8ストリーム入力・イベント正規化
2. 読みバッファ・増分トークナイズ
3. 辞書Trieによる候補生成
4. 候補ラティス更新
5. Morphological Decoder（品詞・活用・接続）
6. Streaming Context Core（Cluster / Claim / Question / Focus）
7. Context-aware Re-ranking
8. Commit Controller（暫定・確定範囲の管理）
9. Punctuation & Formatting
10. 差分出力・診断

通常辞書、ホットワード辞書、文脈状態、スコアリングモデルは分離する。文脈Coreは大規模な汎用推論器ではなく、増分更新可能な小さな状態機械として実装する。

## 5. Streaming Context Core

### 5.1 Context Cluster

Context Clusterは、入力中に形成される話題・対象・意味領域のまとまりである。例として「会議日程」「農地転用」「製品名」「人物」「場所」などを保持する。

各Clusterは次の情報を持つ。

- cluster_id
- cluster_type / domain_tags
- activation_score
- recency
- accumulated_evidence
- associated_terms / entities
- related_claims
- unresolved_questions
- lifecycle_state（active / dormant / archived）

Clusterは単一の直線的な話題列に限定しない。複数Clusterを並行して維持し、入力に応じてFocusを切り替える。

### 5.2 Claim / Evidence

Claimは文脈上の仮説または観測された関係を表す。例:
- 「対象人物は田中」
- 「話題は会議日程」
- 「この『こうしょう』は交渉を指す可能性が高い」

Claimは少なくともsource_event、valid_from、valid_until（未確定可）、confidence_score、supporting_evidence、contradicting_evidenceを持つ。表記候補を意味的に確定した事実と誤認しないよう、hypothesis / accepted / rejected / supersededを区別する。

Evidenceは入力位置・イベント・辞書・ホットワードなどの根拠を参照し、時間的有効性を保持する。後続入力が前提を崩した場合、依存Claimを再評価する。

### 5.3 QuestionとFocus

複数候補が残り、候補間の選択に必要な情報が不足する場合、Questionを生成する。

Questionは:
- question_id
- target_span
- candidate_set
- question_aspect（語義、固有名詞、品詞、係り先、話題など）
- related_cluster_ids
- importance
- resolution_state（open / partially_resolved / resolved / expired）

を持つ。

Questionは関連ClusterへのFocusを増加させる。後続イベントが対象Clusterに関連するEvidenceを追加した場合、そのQuestionと候補スコアを再評価する。全ての曖昧語にQuestionを作るのではなく、候補間のスコア差が閾値以下、または文脈上の重要度が高い場合に限定する。

### 5.4 Focus制御

Focusは単一固定値ではなく、複数Clusterの重み付き分布とする。基本要因:
- 入力との意味的関連
- 直近の活動
- 未解決Questionの重要度
- Claimの証拠強度
- 明示ContextHint
- 時間減衰

新しい入力が既存Clusterと無関係な場合、新Clusterを形成できる。既存Clusterへの復帰も許容する。Focusの切替だけで過去の確定表記を書き換えず、Commit Controllerの再評価可能範囲に従う。

### 5.5 Cluster間関係

Cluster間には関連、包含、継続、対立、同一対象などの関係を持たせられる。v0.2では関係型を拡張可能なIDとして定義し、最小実装ではrelated / same_entity / contrastを扱う。関係は候補選択の補助Evidenceとして使い、単独で変換を決定しない。

### 5.6 忘却と状態上限

文脈状態は無制限に増加させない。Focus、Question重要度、Evidence強度、再利用頻度、時間減衰に基づいてCluster・Claim・Questionを休眠化または破棄する。確定済み出力そのものは、文脈忘却によって消去しない。

忘却は情報を単純にランダム削除するのではなく、再評価に必要な最小要約と参照関係を保持する。状態上限到達時には診断カウンタを出し、決定的な優先順位で整理する。

## 6. 候補生成と形態素デコード

### 6.1 辞書候補

読みTrieから、入力位置に一致する語彙候補を列挙する。候補はsurface、reading、lemma、POS、活用、接続ID、語彙コスト、辞書由来、ライセンス由来を保持する。

### 6.2 ラティス

入力位置をノード、形態素候補をエッジとする。チャンク追加時は末尾の未確定範囲を中心にラティスを更新する。チャンクごとの全再構築を避けるが、文脈変化による再評価が必要な範囲は再計算できる。

### 6.3 基本経路

基本経路探索はViterbi型動的計画法とする。辞書接続コストを基礎とし、Context Coreの出力を追加スコアとして使用する。文節境界や読みの曖昧性が大きい範囲ではビーム探索を許容する。

## 7. 文脈スコアリング

候補cの評価を次の概念式で定義する。全ての項はモデル版ごとに尺度を校正する。

Score(c) =
  - LexicalCost(c)
  - ConnectionCost(c)
  + LocalContext(c)
  + ClusterAffinity(c)
  + EntityContinuity(c)
  + DiscourseCompatibility(c)
  + HotwordEvidence(c)
  + MorphologicalEvidence(c)
  - UncertaintyPenalty(c)

各項の役割:
- LexicalCost: 辞書の語彙頻度・信頼性
- ConnectionCost: 品詞・活用接続の妥当性
- LocalContext: 前後形態素との局所適合
- ClusterAffinity: 現在Focus中のClusterとの関連
- EntityContinuity: 既出固有名詞・対象の継続性
- DiscourseCompatibility: 発話・文書の話題と意味役割の適合
- HotwordEvidence: 有効なホットワードの一致と優先度
- MorphologicalEvidence: 助詞・活用・係り受け手掛かり
- UncertaintyPenalty: 根拠不足や候補競合の度合い

HotwordEvidenceは上限付きの加点とし、接続不可能な候補を強制的に選択させない。ClusterAffinityも一つの話題に過剰適合しないよう飽和関数と上限を設ける。

候補間のスコア差が小さい場合、Questionを維持し、かな表記を暫定採用するか、最小コスト候補を暫定出力する。確定にはCommit条件を要求する。

## 8. ストリーミング再評価とCommit Controller

### 8.1 再評価範囲

新しい入力が到着したとき:
1. 追加読みを解析し、末尾ラティスを更新する。
2. Context Cluster、Claim、Evidence、Question、Focusを増分更新する。
3. 未確定範囲の候補スコアを再計算する。
4. 影響を受けた範囲だけ最良経路を再探索する。
5. 出力差分を生成しrevisionを進める。

### 8.2 暫定・確定

- Provisional: 後続入力で変更可能。
- Stable: 現時点で高信頼だが、明示Commitまでは変更可能。
- Committed: 呼び出し元との契約により変更しない。

確定条件は、右文脈の充足、候補スコア差、境界確定、最大遅延、呼び出し元CommitRequestなどから決定する。単なる経過時間だけで曖昧候補を確定しない。

### 8.3 再評価ウィンドウ

再評価ウィンドウは文節・節・文の各レベルをサポートする。デフォルトは現在の未確定節と、接続・係り受けに必要な直前文脈とする。過去に確定した出力の再変換は明示的なreopen操作がある場合に限る。

## 9. ホットワード

ホットワードは独立したオーバーレイ辞書として管理する。読み、表記、品詞、接続属性、優先度、domain_tag、scope、expiry、sourceを保持する。

- 実行時追加・更新・削除
- session / engine / domainのスコープ
- 有効期限と優先度
- 通常辞書を変更しない
- 同じ読みの複数表記を許容
- 文脈と形態素接続を無視した強制変換は禁止
- 更新は世代付きスナップショットで原子的に反映
- 進行中decodeは開始時の辞書世代を使用

ホットワードが文脈に反する場合は、候補として保持しつつ選択スコアを抑制できる。

## 10. 句読点・表記正規化

句読点推定はStreaming Context CoreとMorphological Decoderの双方から特徴を受け取る。節境界、接続助詞、文末表現、話題転換、文体、入力境界ヒントを利用する。

読点・句点は暫定イベントとして挿入・撤回可能とし、Committed範囲では変更しない。明示入力の句読点は推定より優先する。句読点推定を独立モジュールとして差し替え可能にする。

## 11. 辞書方針とライセンス管理

### 11.1 第一候補

SudachiDictを初期語彙基盤の第一候補とする。Small / Core / Fullの構成があり、Fullは追加の固有名詞語彙を含む。SudachiDictはApache-2.0を掲げ、UniDic由来語彙とNEologdの一部を含む。

ただし、派生配布物・ソース構成によっては語彙CSVと接続行列・文字定義ファイルでライセンスが異なる。したがって、配布対象の各ファイルについて upstream URL、版、SHA-256、著作権表示、適用ライセンスを記録する。ライセンス確認が完了しないファイルは同梱しない。

### 11.2 補完候補

UniDicは書き言葉・話し言葉の語彙・形態素属性を補完する候補とする。GPL/LGPL/修正BSDのトリプルライセンス条件と、採用するファイルごとの表示・再配布条件を確認する。

NEologd系語彙は新語・固有名詞の追加候補とするが、SudachiDictへの包含状況と二重収録を確認し、ライセンス情報を独立管理する。

### 11.3 内部辞書形式

外部辞書形式を実行時に直接公開APIへ漏らさない。インポートツールで内部辞書形式へ変換し、読みTrie、語彙属性、接続情報、由来メタデータを格納する。内部辞書はバージョン・チェックサム付きの読み取り専用バイナリとする。

## 12. C API設計方針

C ABIを公開境界とし、C++内部型を露出しない。不透明ハンドル:
- kd_engine_t
- kd_session_t
- kd_result_t

初期API案:
- kd_engine_create / kd_engine_destroy
- kd_session_create / kd_session_destroy
- kd_stream_push(session, chunk, length, flags, out_update)
- kd_stream_commit(session, input_offset, out_update)
- kd_stream_reset(session)
- kd_decode_batch(session, input, length, options, out_result)
- kd_update_get_text / kd_update_get_diff
- kd_hotword_upsert / kd_hotword_remove
- kd_session_set_context_hint
- kd_result_destroy
- kd_get_version

全関数はstatus codeを返し、メモリ所有権・文字列寿命・スレッド安全性・NULL許容を規定する。C++例外をABI境界の外へ出さない。

## 13. 性能・資源要件

### 13.1 レイテンシ

目標は定常状態で1文節あたり1 ms未満。計測は候補生成・形態素接続・文脈再ランキングを含める。句読点を含む全体時間、イベント投入から差分出力までの時間も別に計測する。

報告値はp50 / p95 / p99、文節長、候補数、ラティスエッジ数、Context Cluster数、未解決Question数、ホットワード数、CPU型番、OS、コンパイラ、ビルド設定とする。

初期化・辞書ロードは別計測。1ms未満は目標であり、実測前に保証値としない。

### 13.2 計算量制御

最大候補数、ビーム幅、最大ラティスエッジ数、Context Cluster数、Question数、再評価ウィンドウ長を設定可能にする。上限到達時は決定的な優先順位で整理し、診断カウンタを返す。

### 13.3 CPU / オフライン

ネットワーク通信を行わない。GPUや外部推論サーバーを必要としない。動的メモリ確保をホットパスで抑え、バッファ再利用と事前確保を優先する。

## 14. テスト・評価

### 14.1 機能テスト

- かな漢字変換・活用・助詞接続
- 同音異義語の文脈依存選択
- 後続入力による過去候補の再評価
- チャンク境界が語・文節の途中にある入力
- 話題の継続・転換・復帰
- 複数Context Clusterの並行維持
- Question生成・Focus遷移・Question解消
- Claim/Evidenceの追加・矛盾・期限切れ
- ホットワードの動的追加・削除・期限・スコープ
- ホットワードと文脈の競合
- 暫定出力の修正・確定後の不変性
- 句読点の挿入・撤回
- 忘却・状態上限
- UTF-8不正入力・長文・辞書破損
- 複数セッションと並行更新

### 14.2 代表的なストリーミングケース

1. 「きしゃが」→「きしゃがきた」: 後続述語に応じて「記者」「汽車」等を再評価。
2. 「こうしょうを」→「こうしょうをすすめる」: 「交渉」「高尚」等を文脈と述語接続から選択。
3. 「あめが」→「あめがふる」: 「雨」を優先し、「飴」の仮説を弱める。
4. 「かみを」→「かみをきる」: 「紙」「髪」等を目的語と述語から再評価。
5. 話題A→話題B→話題A: Clusterを復帰させ、関連語の候補順位を更新。
6. 未知の人名を含む入力にHotwordUpdate: 未確定範囲を再評価し、登録表記を候補へ追加。
7. チャンクが「たべ」/「ました」に分割: チャンク境界を形態素境界と誤認しない。
8. 曖昧な候補を保持したままCommitRequest: 未解決時の保守的確定方針を検証。

### 14.3 品質・性能指標

- CER
- 文節単位正解率
- 同音異義語選択正解率
- ストリーム再評価後の最終正解率
- 暫定出力の修正回数・修正距離
- 句読点Precision / Recall / F1
- Hotword適用正解率
- p50 / p95 / p99 latency
- event-to-update latency
- peak RSS、辞書ロード時間、状態メモリ量

## 15. 実装ロードマップ

1. 辞書ライセンス監査・語彙インポーター・内部辞書形式
2. UTF-8読みバッファ・Trie候補生成
3. 形態素ラティス・接続コスト・Viterbi
4. ストリームイベント・差分出力・Commit Controller
5. Context Cluster / Claim / Evidence / Question / Focus
6. Context-aware Re-rankingと遡及再評価
7. Hotword Overlayと世代スナップショット
8. 句読点・表記正規化
9. C API / ABIテスト
10. 精度・遅延・メモリ・fuzz・並行性試験

## 16. v0.2受け入れ条件

- 独立したチャンク入力を継続的に処理できる。
- チャンク境界をまたぐ語・文節を正しく扱える。
- 新しい文脈Evidenceにより未確定範囲の候補を再評価できる。
- Context ClusterとFocusを複数保持できる。
- 曖昧性をQuestionとして追跡し、後続入力で解消できる。
- HotwordUpdateを未確定範囲へ反映できる。
- 暫定・確定出力を区別し、確定範囲を不意に変更しない。
- CPUのみ・完全オフラインで動作する。
- 1文節1ms未満の性能目標を再現可能に測定できる。
- 辞書の全配布ファイルについて由来・ライセンスを追跡できる。

---
v0.2は設計仕様であり、辞書採用の最終判断と性能達成は監査・実装・ベンチマーク後に確定する。
