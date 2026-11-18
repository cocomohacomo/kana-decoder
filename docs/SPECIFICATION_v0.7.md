# kana-decoder 仕様書 v0.7

- Status: Design specification
- Version: 0.7.0
- Core: C++ / public ABI: C API
- Runtime: CPU only / fully offline
- Primary latency target: 1 ms未満 / 文節（定常状態）
- Core characteristic: Streaming-Native Contextual Kana-Kanji Decoding with Cognitive Streaming Pipeline, Branch-based Candidate Management and Bounded-Work Execution

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


## 17. Branch-based Candidate Management

### 17.1 目的

かな文字列から生成される正文化候補を単一の最尤候補へ早期に縮退させず、複数の仮説として並行保持する。各候補をGitのブランチ管理に似たBranchとして扱い、入力イベントの到着ごとに候補を再評価する。

本機構はSURGEのBranch / Focus / Forgettingの考え方を参考にするが、kana-decoder独立の決定的な候補管理機構として実装する。Branchは意味Clusterそのものではなく、特定の読み範囲に対する正文化経路の仮説である。各Branchは複数のCluster・Claim・Evidenceを参照できる。

### 17.2 Branchの構成と状態

Branchは少なくとも次を保持する。

- branch_id: セッション内で一意なID
- parent_branch_id: 分岐元。初期Branchではnull
- candidate_path: 読み範囲に対応する形態素・表記候補列
- covered_input_range: 対象入力オフセット範囲
- score_vector: スコア項目ごとの評価値
- confidence_score: 候補間順位付けに用いる内部確信度スコア
- evidence_refs: 支持・反証Evidenceへの参照
- cluster_refs: 関連Context Clusterへの参照
- created_at_sequence / updated_at_sequence
- revision
- lifecycle_state: main / active / archived / committed / pruned
- archive_reason / pruning_reason

確信度スコアは順位付け用スコアであり、校正済み確率と同一視しない。確率として公開する場合は、別途校正方法・対象データ・モデル版を明示する。

### 17.3 Main / Active / Archive

候補集合は次の領域に分ける。

- Main: 現時点で最上位の正文化候補。通常は1つ。
- Active Branches: Mainに次ぐ有力候補群。Focusの対象として維持する。
- Archive: 現時点で上位集合から外れた候補。通常の毎回の再探索対象から外すが、候補情報と由来を保持する。
- Committed: Commit Controllerによって確定した出力。候補管理上の履歴として不変に扱う。
- Pruned: 重複・無効・保持期限切れ等により削除された候補。診断設定で監査情報のみ残せる。

Mainは原則1つとする。ただし候補間の評価が同点で決定的タイブレーク後も意味的に区別できる場合、代表Mainを1つ選びつつ、他候補をActiveに残す。曖昧性をMain選択だけで解消したと扱ってはならない。

### 17.4 入力更新時の処理

新しいTextChunk、BoundaryHint、ContextHint、HotwordUpdate等を処理するたび、次の順序で候補を更新する。

1. 入力範囲と既存Branchの被覆範囲を照合する。
2. MainおよびActive Branchから、形態素候補・辞書候補・接続可能な後続候補を増分展開する。
3. 新しいEvidenceを追加し、依存するClaim / Question / Clusterを更新する。
4. 未確定範囲に関係するBranchを再スコアリングする。
5. 重複候補を正規化し、同一候補経路を統合する。
6. 決定的な順位規則によりMainを選択する。
7. Active上限・最低保持条件に基づき、下位候補をArchiveへ移す。
8. Archive候補の再活性化条件を評価する。
9. FocusをMainと上位Active Branchに対応するCluster・Evidenceへ配分する。
10. 出力差分とrevisionを生成する。

候補展開は原則MainおよびActiveから行う。Archive全件を毎回展開・再評価してはならない。

### 17.5 Branch Scoreと順位付け

Branch全体の評価は、構成する形態素候補のスコア、接続妥当性、読み範囲の整合性、文脈Evidence、過去の再評価履歴を統合する。単純な形態素スコアの合計だけでなく、候補経路の長さ・範囲・重複を正規化し、短い候補が不当に有利または不利にならないようにする。

順位付けの同点解消順序は固定する。推奨順序:
1. 形態素接続違反が少ない候補
2. 未解決の必須制約違反が少ない候補
3. 校正前の統合スコア
4. Evidenceの新しさと有効性
5. 辞書・候補IDによる安定した最終タイブレーク

候補の確信度が近接する場合はQuestionを維持する。Mainの選出は表示上の代表決定であり、意味上の確定を意味しない。

### 17.6 Branch PruningとArchive

BranchをActiveから外す操作と、候補データを削除する操作を区別する。

- Archive移動: Focus対象から外し、通常の候補展開・再評価から除外する。再活性化に必要な候補経路、スコア要約、Evidence参照、親子関係は保持する。
- Pruning: 重複、構文的無効、入力範囲の不整合、明示的な保持期限切れ、または容量制限により候補を削除する。
- Commit: 出力範囲を確定し、以後の通常更新から不変にする。

Archiveへの移動は候補の否定・誤り確定ではない。新しいEvidenceによる復帰可能性を残す。

### 17.7 Archiveからの再活性化

Archive候補は、次のいずれかを満たす場合に再活性化候補となる。

- 新しい入力が候補の未確定読み範囲または後続接続条件に関連する。
- 関連ClusterのFocusが上昇した。
- 既存Claimの有効性が変化した、または矛盾が解消した。
- HotwordUpdateやContextHintが候補を直接支持する。
- Main / Activeの候補が形態素接続・文脈整合性を失った。

再活性化時は保存済みスコアをそのまま信用せず、現在有効なEvidenceと辞書世代に基づき再評価する。再活性化候補はActive上限の対象として順位付けする。

### 17.8 Focusとの連携

FocusはMain Branchだけに固定しない。Mainおよび上位Active Branchが参照するCluster・Question・Evidenceへ重み付きで配分する。Branchの順位変動に応じてFocusも更新する。

Archive Branchは通常Focusから除外する。ただし再活性化判定に必要な軽量メタデータは参照可能とする。Focusの変化だけでCommitted出力を変更してはならない。

### 17.9 Branchの共有と差分表現

複数Branchが共通する形態素列・読み範囲を持つ場合、共通prefixや共通suffixを共有し、全文候補の重複保持を避ける。実装は木またはDAGを許容するが、外部APIからは安定したBranch IDと候補列として観測できること。

Branch間の関係として forked_from / equivalent / competing / supersedes を定義可能にする。候補統合時には統合元IDを監査情報に残す。

### 17.10 上限・忘却・決定性

Active Branch数、Archive数、候補経路長、再活性化回数に上限を設ける。初期実装では設定可能な上限値を用い、既定値はベンチマークで決める。上限到達時の整理は次の優先順位で決定的に行う。

1. Committed範囲と整合する候補を保護
2. 形態素・接続の妥当性
3. 現在のBranchスコア
4. Evidenceの有効性・新しさ
5. Main/Activeとしての継続期間
6. 安定したBranch IDによるタイブレーク

Archiveの保持期限切れは設定可能とし、期限切れで削除する場合も、診断モードでは削除理由とカウンタを取得できるようにする。メモリ圧迫時にMainやCommittedの整合性を犠牲にしてはならない。

同一イベント列、辞書世代、モデル版、設定から同一のBranch状態遷移と出力を得る。

### 17.11 Commitとの境界

Branchは未確定範囲の仮説を管理する。Commit Controllerが入力範囲を確定した時点で、その範囲のMain候補をCommitted出力へ移す。確定範囲に対するActive / Archive Branchは通常の再評価対象から外す。

明示的なreopen操作を将来追加する場合は、通常のstream updateとは別API・別revision系列で扱い、既存Committed出力を暗黙に書き換えない。

### 17.12 診断情報

診断モードでは少なくとも次を取得できるようにする。

- Main Branch IDとスコア
- Active Branch一覧と順位
- Archiveへの移動数・理由
- 再活性化数・理由
- Pruning数・理由
- Branch展開数・統合数
- 候補ごとのEvidence参照
- Branch状態遷移履歴
- Active / Archiveの最大同時保持数

### 17.13 受け入れ条件

- 同じ読み範囲に対して複数の正文化候補を並行保持できる。
- Mainを1つ選び、上位Active Branchを複数保持できる。
- 新しい入力イベントごとに未確定候補を再評価できる。
- 下位候補を削除せずArchiveへ移動できる。
- 新しいEvidenceでArchive候補を再活性化できる。
- Archive候補を毎回全件展開せず、通常処理の計算量を制御できる。
- 候補統合・分岐・順位変更の履歴を診断できる。
- Branchの整理によってCommitted出力が変更されない。
- 上限到達時の整理と同点解消が決定的である。
- 同一イベント列・同一設定で同一結果を再現できる。

## 18. v0.3受け入れ条件の追加

- Main / Active / Archive / Committedの状態遷移が実装可能な形で定義されている。
- Branch展開・評価・整理・再活性化の順序が規定されている。
- Branch Scoreを確率と誤認しない。
- Archive移動とPruningを区別する。
- Branch上限・Archive保持・Forget処理が決定的である。
- FocusおよびCommit Controllerとの責務境界が明確である。


## 19. Bounded-Work Architecture（イベント当たり計算量の上限制御）

### 19.1 設計目標と計算量契約

本アーキテクチャは、ストリーム全体の長さ、過去の入力数、Archiveの累積履歴に比例して、次の入力イベントの処理時間・メモリ使用量が増加しないことを目標とする。

無制限長入力・無制限出力を含む全処理を数学的に厳密なO(1)とすることはできない。したがって計算量契約を次のように定義する。

- **Hot-path update**: 最大イベントサイズ、候補数、再評価窓、文脈状態数等の固定上限の下で、ストリーム長に対してO(1)。
- **Input/output transfer**: 受け取る・返すデータ量に比例するO(n)。固定サイズチャンクに分割し、1イベント当たりの上限を設ける。
- **Initialization / dictionary loading**: 辞書サイズに依存する。ストリーム更新ホットパスから分離する。
- **Explicit batch / reopen / diagnostics**: 呼び出し元が明示した場合のみ実行し、通常ストリーム更新のレイテンシ契約に含めない。各APIにも上限を設ける。

ここでO(1)とは、入力ストリーム全体の長さや過去イベント数に依存しない上限付き処理を意味する。定数は構成時に確定し、実行中に無制限拡張しない。各上限を増やせば定数時間の定数項とメモリ使用量も増える。

### 19.2 固定容量の状態モデル

全てのホットパス状態は固定容量配列、リングバッファ、固定サイズプールで保持する。実行中の自動拡張、全履歴走査、無制限キューを禁止する。

初期実装の上限候補（最終値はベンチマークで確定）:

| 状態 | 初期上限候補 | 上限超過時 |
|---|---:|---|
| 1入力イベント | 64 Unicode code points | 固定チャンクへ分割 |
| 未確定読み窓 | 64 code points | 古い部分をCommit要求または保守的に確定 |
| 同時Branch総数 | 8（Main含む） | 下位をArchiveへ |
| Active Branch | 4（Mainを含む） | 下位をArchiveへ |
| Archive Branch | 16 | 低優先候補からPrune |
| Branchあたり形態素数 | 32 | 窓を分割し、境界状態だけ引き継ぐ |
| Context Cluster | 16 | 低Focus Clusterを要約・休眠・破棄 |
| Claim | 32 | 低重要度・期限切れから忘却 |
| Evidence | Claimあたり4 | 集約要約へ圧縮 |
| Open Question | 8 | 低重要度を部分解決・休眠・破棄 |
| Focus対象 | 4 | 上位4件に制限 |
| Hotword Overlay | 128 | スコープ・優先度・期限で整理 |
| 1候補の再活性化検査 | 4件/イベント | 残りは後続イベントへ延期 |
| 1イベントの候補展開 | 16遷移 | 上位遷移を残し残りを打ち切る |

上記は初期の設計上限であり、精度とメモリの実測を経て調整可能とする。ただし上限を外して無制限化する設定は禁止する。設定値はEngine生成時に検証し、固定容量を確保する。

### 19.3 固定予算のイベント処理

1イベントの処理は次の固定予算フェーズで実行する。

1. **Ingest**: 最大イベント長だけをUTF-8検証・正規化する。
2. **Token/Lattice Update**: 末尾の固定窓だけを更新する。
3. **Branch Expansion**: MainとActiveを起点に最大N個の遷移だけ展開する。
4. **Scoring**: 最大Branch数×最大窓長の組合せだけを評価する。
5. **Context Update**: 最大Cluster / Claim / Question / Evidence件数だけを更新する。
6. **Archive Probe**: 固定件数のArchive候補だけを再活性化検査する。
7. **Selection**: 固定長候補配列内でMain・Activeを選択する。
8. **Output Diff**: 変更された未確定窓だけを差分化する。

各フェーズは処理件数の上限を持つ。上限に達した場合は、そのイベント内で追加探索を行わず、打ち切り・延期・要約のいずれかを決定的に選ぶ。イベントごとの予算超過を許す無制限whileループや、キューが空になるまで処理する実装は禁止する。

### 19.4 データ構造と探索コスト

- Branch: 固定長Branchスロット配列。Main/Active/Archiveは状態タグで区別する。
- Branch path: 固定長ノードプールによる共有prefix/DAG。ノード数に上限を設ける。
- Context: 固定長Cluster/Claim/Question配列と固定長参照配列。
- Evidence: 個別履歴を無制限に追加せず、上位Evidenceと集約統計を保持する。
- Focus: 最大4要素の固定配列。
- Event buffer: 固定長リングバッファ。
- Hotword: 固定容量オーバーレイ。検索は最大プローブ数を持つ固定表、または最大分岐数を制限したTrieを使う。
- Dictionary Trie: 読み長、ノード次数、探索深さに上限を設定する。各文字位置の子探索も固定上限内にする。
- Output: 未確定窓の固定バッファと、呼び出し元所有のCommitted出力領域を分離する。

固定配列内の線形走査は、配列容量が構成時に固定されるため、ストリーム長に対してO(1)である。ハッシュテーブルは最大プローブ数を制限し、上限到達時は探索を打ち切る。無制限の再ハッシュや容量拡張は禁止する。

### 19.5 Incremental ContextとForgetting

Context Coreはイベントごとに全Clusterや全Claimを再走査しない。入力イベントから直接参照されるCluster、候補が参照するClaim、影響を受けるQuestionのみを更新する。参照先探索も固定件数に制限する。

忘却処理を全状態の定期走査として実装してはならない。各イベントで固定数のスロットだけを巡回するclock-hand方式を用い、Focus、重要度、Evidence要約、最終参照sequenceを評価する。削除・休眠の候補選択は固定件数の範囲内で行う。

期限切れ判定はイベント時刻との比較で行い、期限切れ要素の全件探索を行わない。期限管理は固定長のタイミングホイールまたはスロット単位の遅延チェックを用いる。

### 19.6 Branch管理の定数時間化

- Main / Activeの評価対象は最大Branch数に固定する。
- 候補展開はイベントごとに最大展開数を固定する。
- 順位付けは固定長配列の選択ソートまたは挿入処理で行う。計算量は固定上限に依存し、ストリーム長には依存しない。
- Archive全件の再評価は禁止する。
- Archiveからの再活性化検査はイベント当たり最大K件とし、リングカーソルで検査位置を進める。
- 1候補の再活性化条件が成立した場合も、イベント当たりの追加展開予算を消費する。
- Branch pathの長さとノード数に上限を設ける。上限に達した場合は古い範囲をCommit境界で切り離し、境界状態だけを保持する。
- Branchの重複判定は固定長のハッシュ指紋と限定比較で行い、全文比較をホットパスで行わない。

### 19.7 遅延処理・段階的探索

固定予算内に収まらない探索は、無制限に同じイベント内で続行しない。優先度付きの固定長Pending Work Queueに登録し、次イベント以降に最大M件ずつ処理する。

優先順位:
1. Mainの形態素・接続整合性維持
2. 現在のActive Branchの評価
3. 重要度の高いOpen Question
4. Focus中Clusterの依存Claim再評価
5. Archive候補の再活性化
6. 低Focus Clusterの整理

Pending Work Queueが満杯の場合は、優先度の低い項目を統合・破棄し、診断カウンタを増やす。処理待ちがあることを診断情報に公開する。遅延処理のためにMainの整合性やCommitted出力の不変性を損なってはならない。

### 19.8 長文・Commit境界

長文全体を1つの未確定ラティスとして保持しない。固定長の未確定窓をスライドさせ、Commit済み出力は呼び出し元の出力領域へ移す。次の窓へ渡す状態は、直前の形態素接続状態、限定されたCluster要約、未解決Questionの上位項目など固定長の境界サマリーに限る。

出力全文の連結・コピーは出力量に比例するためO(n)であり、ホットパスのO(1)契約から分離する。差分APIは変更区間のみを返し、呼び出し元が必要に応じて全文を構成する。

### 19.9 メモリとアロケーション契約

- Engine生成時に固定容量メモリを確保する。
- Session生成時にセッション用固定プールを確保する。
- stream_pushのホットパスではヒープ確保・解放を行わない。
- 文字列・Branch・Evidenceの可変長領域は固定長スロットまたは共有ノードプールを使う。
- 上限超過は明示的なstatus / diagnostic counterで報告する。
- OOMを回避するための暗黙の容量拡張は禁止する。
- Engine共有辞書は読み取り専用、Session状態はセッション単位で隔離する。

### 19.10 性能検証契約

次の負荷でイベント当たり処理時間とメモリが増加しないことを検証する。

- 短文を長時間連続投入し、総イベント数だけを増やす。
- 同音異義語を連続投入し、Branch生成を上限まで発生させる。
- 話題を頻繁に切り替え、Cluster / Questionを上限まで発生させる。
- Hotwordを上限まで登録する。
- Archive候補を上限まで蓄積し、毎イベントの検査数が固定であることを確認する。
- 長文を固定チャンクで投入し、窓サイズとRSSが増加しないことを確認する。
- Pending Work Queueを飽和させ、処理予算超過や無限ループがないことを確認する。

測定値:
- event update p50 / p95 / p99 / max
- 1イベント当たり処理ステップ数
- Branch展開数・再評価数・Archive probe数
- Pending Work Queueの最大深度
- peak RSSとSessionあたり固定メモリ
- ホットパスのallocation count（目標0）
- 入力総イベント数に対する更新時間の傾向

「常に一定」は平均値だけで判断しない。固定上限を超える入力を拒否・分割・延期する動作を含め、最悪ケースで処理ステップ数が定義済み予算を超えないことをテストする。

### 19.11 受け入れ条件

- すべてのホットパス状態に有限の容量上限がある。
- 1イベント当たりのBranch展開数、再評価数、Archive検査数、Context更新数に上限がある。
- 入力総量・過去イベント数の増加で、1イベント当たり処理ステップ数が増えない。
- Archive全件走査、全Cluster走査、全履歴再計算が通常更新に存在しない。
- ホットパスの動的メモリ確保が0である。
- 上限超過時に決定的な打ち切り・延期・忘却を行う。
- 長文ストリームでSessionのメモリ使用量が上限内に留まる。
- Committed出力の不変性を保つ。
- 同一イベント列・同一上限設定で同一の整理結果を再現する。
- O(1)契約の対象外となる初期化・辞書ロード・全文出力・明示Batch処理を明確に分離する。



## 20. 認知フレームワーク型ストリーミング処理詳細

### 20.1 設計目的

かな入力から正文化までを、単発の文字列変換ではなく、入力イベントに応じて複数の言語仮説と文脈仮説を更新する認知パイプラインとして定義する。

本処理は二つの相互作用するループから構成する。

- **言語形式ループ**: 読み、語彙、形態素境界、品詞、活用、接続可能性を更新する。
- **文脈認知ループ**: 形態素候補から意味・話題・対象・談話関係の仮説を形成し、Evidence / Question / Focusを更新して言語候補の順位へフィードバックする。

一方のループが他方を一方向に支配してはならない。形態素解析は文脈認知の入力であり、文脈認知は形態素解析の補助Evidenceである。形態素的に不可能な候補を文脈スコアだけで採用してはならない。

### 20.2 パイプライン

標準処理段階を次のように定義する。

1. Input Eventization
2. Reading Normalization / Incremental Buffering
3. Lexical Candidate Generation
4. Morphological Lattice Construction
5. Morphological Decoding
6. Semantic / Context Recognition
7. Context-aware Re-ranking
8. Orthographic Selection
9. Commit Control
10. Punctuation / Formatting / Delta Output

各段階は固定容量の入力・出力スロットを持つ。段階間で全文コピーを行わず、入力範囲、候補ID、状態IDを参照する。段階の出力は確定結果ではなく、revisionを持つ仮説として扱う。

### 20.3 Input Eventization

入力APIから受け取ったUTF-8バイト列をイベント化する。イベントにはsequence_no、byte_offset、normalized_codepoint_offset、timestamp（任意）、source_id（任意）、boundary_hint、context_hintを付与する。

UTF-8検証と文字境界の保護を行い、チャンク末尾の未完シーケンスは次イベントへ持ち越す。正規化後の文字位置と原入力byte範囲の対応表を保持し、差分出力・診断・Hotword適用範囲を原入力へ対応付けられるようにする。

チャンク境界を形態素境界・文節境界とみなしてはならない。BoundaryHintは確定的境界ではなく、信頼度付きEvidenceとして扱う。

### 20.4 Reading Normalization / Incremental Buffering

入力を内部読み表現へ正規化する。ひらがな・カタカナの扱い、全半角、濁点・半濁点、長音、促音、拗音、記号をポリシー化し、原文保持が必要な情報は別フィールドへ保存する。

未完の語幹、活用語尾、複合語の継続可能性を保持する。読みバッファは固定長リングとし、確定済み範囲を再解析窓から切り離す。読みの正規化は漢字候補の選択を行わない。

### 20.5 Lexical Candidate Generation

辞書Trieを読み位置から走査し、語彙候補を生成する。候補の出所はsystem dictionary、user dictionary、hotword overlay、unknown-word generatorに分類する。候補にはreading span、lexical_id、surface、lemma、POS、conjugation、left/right connection id、word cost、dictionary generation、source flagsを保持する。

未知語生成は任意の連続文字列を無制限に語彙化せず、文字種パターン・最大長・候補数を制限する。人名・地名等の未知固有名詞候補は、かな表記を保持したまま後続Hotwordや文脈Evidenceで再評価可能とする。

Hotwordは独立Overlayとして候補生成へ参加する。Hotword一致は候補の存在を追加する根拠であり、最終選択の強制命令ではない。

### 20.6 Morphological Lattice Construction

読み位置を頂点、形態素候補を有向エッジとするラティスを構築する。各エッジは読み範囲、語彙ID、品詞・活用、接続ID、語彙コスト、候補出所を参照する。

ラティスは末尾未確定窓を中心に増分更新する。既存エッジは入力範囲と辞書世代が有効であれば再利用する。新しい読みが追加された場合は末尾の到達可能範囲のみ拡張する。読み境界・辞書世代・Hotword世代が変わった場合は影響範囲を限定して再構築する。

候補生成時点では同音異義語の意味的優劣を決定しない。語彙候補の存在と形態素接続可能性を保持する。

### 20.7 Morphological Decoding

第一段階の形態素デコードは辞書由来のword costとconnection costを用いるViterbi型動的計画法とする。実装は最良経路だけでなく、固定幅Kの上位経路または同等のN-best候補を保持する。上位経路はBranch Managerへ引き渡す。

Juman++型のビーム探索や学習済み言語モデルは、交換可能なMorphologicalScorerとして将来追加できる。ただし初期CPU軽量構成では辞書・接続行列・小さな線形特徴スコアを基準とし、大規模ニューラル推論を必須にしない。

形態素デコードの出力は、読み範囲、形態素列、局所コスト、接続整合性、未確定境界を含む。N-best順位は確率と呼ばず、校正が別途完了するまでranking scoreとして扱う。

### 20.8 Semantic / Context Recognition

文脈認知層は形態素経路から軽量な意味特徴を抽出する。初期実装では辞書属性、品詞列、活用、格助詞、係助詞、述語、固有名詞属性、語彙ドメインタグ、Hotwordタグ、直近の確定表現を用いる。

認識対象は以下とする。

- Topic / Domain: 話題・領域の仮説
- Entity: 人物・組織・場所・製品等の対象仮説
- Predicate / Relation: 述語と項・関係の仮説
- Discourse State: 説明、質問、依頼、列挙、訂正等の談話機能の仮説
- Temporal Scope: ClaimやEntity属性が有効な入力範囲
- Ambiguity: 候補間で未決定の意味的側面

これらは確定事実ではなく、source spanとEvidenceを持つClaimとして管理する。文脈情報が不足する場合、無理に意味フレームを生成せず、未確定状態を維持する。

### 20.9 Question / Focus生成規則

Questionは次のいずれかを満たす場合に生成候補とする。

1. 上位候補間のスコア差が設定閾値以下。
2. 候補の選択によってEntity、Topic、Predicate等の解釈が変わる。
3. 後続入力により解消可能な未確定依存関係がある。
4. 誤変換コストが高い固有名詞・専門語・数値表現で根拠が不足する。

Questionはtarget_span、candidate_ids、question_aspect、related_cluster_ids、importance、creation_sequence、resolution_stateを保持する。FocusはQuestionの重要度と候補のCluster関連度から更新する。全曖昧語にQuestionを作らず、固定上限内で選別する。

### 20.10 Context-aware Re-ranking

候補cの最終順位スコアは、形態素基礎スコアと文脈特徴を分離して計算する。

BaseMorphScore(c) = -WordCost(c) - ConnectionCost(c) + LocalMorphFeatures(c)

ContextScore(c) = SemanticCompatibility(c) + ClusterAffinity(c) + EntityContinuity(c) + DiscourseCompatibility(c) + HotwordEvidence(c) + TemporalConsistency(c)

RankScore(c) = BaseMorphScore(c) + bounded(ContextScore(c)) - UncertaintyPenalty(c)

ContextScoreの各特徴は上限・飽和関数を持つ。文脈スコアは形態素接続のハード制約を覆せない。異なるスコアラー間で尺度が異なる場合はモデルごとの重み・正規化を用い、単純な生スコア比較を禁止する。

候補順位だけでなく、上位候補間のmargin、Evidenceの独立性、根拠の新しさ、反証Evidenceを記録する。候補が一つだけ残っても、その候補の根拠が未知語フォールバックだけなら高確信とは扱わない。

### 20.11 Orthographic Selection

形態素候補が選ばれた後、語彙ID・lemma・読み・品詞・文脈タグに基づいて表記候補を選択する。表記選択は語義選択と区別する。同一語義の漢字・かな・送り仮名・数字表記の違いを、意味候補Branchと混同しない。

表記候補は次の順で評価する。

1. 明示的なユーザー指定・辞書の正規表記
2. 有効なHotwordの表記指定
3. ドメイン・文体に応じた表記規則
4. 一般的な表記頻度・可読性
5. 根拠が弱い場合のかな表記フォールバック

表記正規化で原入力情報を失わない。誤った漢字の断定を避けるため、低信頼候補はかな表記を許容する。

### 20.12 Commit Control

Commit Controllerは、候補順位・margin・右文脈の充足・形態素境界の安定性・Questionの解決状態・最大未確定窓を参照する。

- Provisional: 再評価可能。差分更新を許可。
- Stable: 現時点で十分安定。通常は保持するがCommit前は再評価可能。
- Committed: 呼び出し元契約により不変。通常イベントから変更禁止。

時間経過だけで曖昧性を確定しない。最大窓に達した場合は、低信頼候補をかな表記として保守的に確定する、呼び出し元へCommit要求を返す、または入力を分割する。誤った漢字を強制確定するより、未確定・かなフォールバックを優先する。

### 20.13 Punctuation / Formatting

句読点推定は形態素・述語・節境界・接続助詞・文末表現・談話状態・BoundaryHintを特徴として扱う。句読点は独立のOutput Eventとして保持し、挿入・撤回をrevision付きで通知する。明示入力された句読点は推定結果より優先する。

表記整形と句読点推定は分離モジュールとし、辞書・文体・アプリケーションのポリシーを交換可能にする。

### 20.14 再評価の依存関係

各候補・Claim・Questionはsource spanと依存参照を保持する。新規イベントでEvidenceが追加された場合、直接依存するQuestion、Claim、Branchだけを再評価対象にする。全履歴を再デコードしない。

再評価理由をdiagnostic eventとして記録する。例: new_right_context、focus_changed、hotword_generation_changed、claim_invalidated、boundary_confirmed。Committed範囲は通常の依存再評価から除外する。

### 20.15 固定予算への適合

各段階はイベント当たりの最大候補数・最大探索深さ・最大再評価数を共有BudgetContextから取得する。予算を使い切った場合は、優先順位に従って処理を延期または候補をArchiveへ移す。未処理作業数と打ち切り理由を診断情報に残す。

UTF-8入力走査・入力コピー・全文出力はデータ長に比例するためO(n)であり、イベント当たりホットパスの固定予算契約と区別する。イベントを固定長に分割した後の状態更新は、固定容量上限の範囲内で処理する。

### 20.16 ストリーミング例

入力イベント列:

- E1: 「きしゃが」
- E2: 「きた」
- E3: 「えきに」
- E4: 「ていしゃした」

E1では「記者」「汽車」「帰社」等の候補を生成し、助詞「が」との接続を確認する。語義を確定せず、必要に応じQuestionを作成する。

E2で「来た」に相当する述語候補が追加される。候補の形態素接続・意味整合性を更新するが、「記者が来た」「汽車が来た」はともに成立するため、曖昧性を維持する。

E3/E4で「駅」「停車」に関するEvidenceが追加される。交通ClusterのFocusが上昇し、「汽車」候補の意味整合性が強くなる。Mainを更新し、他候補はActiveまたはArchiveに移す。

右文脈と境界が安定し、Questionが解消した段階で「汽車が来た。駅に停車した。」等の表記をCommit可能とする。実際の句読点は文体・節境界ポリシーに従う。

### 20.17 初期実装と拡張境界

初期実装では、辞書Trie、Viterbi、固定幅N-best、格助詞・品詞・述語を用いたルールベース特徴、固定容量Context Core、線形再ランキングを採用する。

後続拡張として、学習済み形態素スコアラー、小型言語モデル、係り受け特徴、ドメイン別表記モデルを追加できる。ただし、CPUのみ・オフライン・固定予算の基本契約を維持し、モデルを追加する場合は推論時間・メモリ・決定性を個別に検証する。

### 20.18 追加受け入れ条件

- 形態素候補生成と意味的候補選択が別責務として実装されている。
- N-bestまたは固定幅Branchを保持し、早期の単一候補化を避ける。
- 文脈スコアだけで形態素接続のハード制約を覆せない。
- 語義選択と同一語義内の表記選択を区別する。
- 不確実な場合にかな表記へフォールバックできる。
- Questionが候補の曖昧な側面と対象範囲を参照する。
- 後続Evidenceによる再評価が依存範囲内に限定される。
- Commit済み出力が通常更新で変化しない。
- 句読点の挿入・撤回が文字列変換と独立した差分イベントとして扱える。
- 同一入力・辞書世代・モデル世代・設定から同一結果を再現できる。


## 21. 最終出力契約と多言語辞書モデル

### 21.1 最終目的

本ライブラリの最終アウトプットは、純かなテキストを入力として、対象言語の正書法・語彙・文法・文脈・表記規則に従い、漢字、ひらがな、カタカナ、ラテン文字等の文字体系、数字、記号、句読点を適切に組み合わせた正文を生成することである。出力は漢字かな交じり文に限定しない。言語パックが定義する文字体系・正書法に従い、必要な文字種を混在させた自然な文章を生成する。

### 21.2 入力契約

入力は純かなテキストを基本とし、漢字等の正書法表記を含まない読み表現として扱う。内部では入力文字列を保持しつつ、言語パックが定義する読み正規化形式へ変換する。「かな」は入力表現の契約であり、内部の言語モデル・辞書・形態素体系を日本語に固定する意味ではない。異なる言語を扱う場合は、その言語の読み表現をかな入力へ対応付けるInput Encoding Adapterを用意する。読み表現が一意に対応付けられない場合、Language Packまたは呼び出し側から言語・読み方式を明示する。

### 21.3 出力文字種

Rendererは漢字および言語固有の表意文字、ひらがな・カタカナ、ラテン文字等のアルファベット、数字（算用数字・漢数字・言語固有数字表記）、単位・通貨・数学・技術記号、句読点・括弧・引用符・区切り記号、空白・改行・段落境界、言語固有の約物・書字方向・禁則を扱う。どの文字種をいつ使用するかはLanguage PackのOrthography PolicyとFormatting Policyで決める。コアが日本語固有の漢字優先規則を全言語へ適用してはならない。

### 21.4 言語パック換装モデル

辞書モデルは言語ごとに独立したLanguage Packとして換装可能にする。Coreは特定辞書形式・品詞体系・接続行列・表記慣習へ直接依存しない。Language PackはInput Encoding Adapter / Reading Normalizer、Lexicon Provider / Trie Index、Morphological Schema / Token Attribute Mapper、Connection Model / Morphological Decoder Adapter、Lexical and Morphological Scorer、Semantic Feature Mapper / Language-specific Context Features、Orthographic Lexicon / Variant Policy、Number and Symbol Formatter、Punctuation and Boundary Model、Unicode Normalization / Script Policy、Pack Manifest / Version / License Metadataを提供する。

### 21.5 言語非依存Coreと言語依存Packの境界

CoreはStream Event管理、Branch / Main / Active / Archive、固定予算・固定容量制御、Candidate lifecycle / Revision / Commit、汎用Evidence・Question・Focus管理、汎用スコア合成インターフェース、差分出力・診断・C ABIを担当する。Language Packは読みの解釈・正規化、辞書検索・語彙候補、品詞体系・活用体系・接続制約、言語固有の形態素デコード、意味特徴の抽出規則、漢字・かな・アルファベット等の表記選択、数字・単位・記号・句読点の表記規則を担当する。Context Coreは言語固有特徴を直接解釈せず、Packが出力する共通Feature IDと型付き属性を受け取る。

### 21.6 Language Pack識別と選択

Manifestはlanguage_id、pack_id、pack_version、core_abi_version、dictionary_version、model_version、reading_encoding_id、normalization_profile、orthography_profile、license_manifest、resource_limitsを持つ。PackはEngine生成時またはSession生成時に明示選択する。初期仕様では自動言語判定を必須としない。明示指定がない場合はEngineのdefault packを使う。稼働中SessionのPackを暗黙に差し替えてはならない。変更する場合はSessionをDrainまたはResetし、辞書世代・モデル世代を更新する。

### 21.7 正文化パイプラインへの統合

既存10段階を維持し、言語依存処理をPack経由で呼び出す。Input Eventizationは共通Core、Reading Normalization・Lexical Candidate GenerationはPack、Morphological LatticeはPack Adapterと共通候補格納、Morphological DecodingはPack Decoder Adapter、Semantic / Context RecognitionはPack Feature Mapperと共通Context Core、Context-aware Re-rankingは共通スコア統合とPack Scorer、Orthographic SelectionはPack、Commit Controlは共通Core、Punctuation / FormattingはPack Rendererと共通差分出力が担当する。

### 21.8 数字・記号・句読点の正文化

数詞・助数詞・日付・時刻・金額・割合・単位・数式・略語は、読み・意味・文脈を持つ候補として扱う。数字・記号は単純な後処理置換に限定しない。桁区切り、小数点、符号、単位、通貨、日付順序などはLanguage PackとFormatting Policyに従う。数値の意味を変える正規化は禁止する。曖昧な数値読みはQuestionとして保持し、必要に応じ原読み表記を残す。句読点・括弧・引用符等は言語固有の配置・間隔・禁則に従う。明示入力と推定挿入のprovenanceを区別し、推定記号のみ未確定範囲で撤回可能とする。

### 21.9 辞書・モデルのライセンスと配布

Language Packごとに辞書・モデル・接続行列・文字定義・変換規則の由来とライセンスを個別管理する。Manifestにライセンス識別子、著作権表示、upstream、version、hash、再配布条件を記録する。CoreのMITライセンスと外部Language Packのライセンスは区別し、Packのライセンス条件がCore全体へ暗黙に波及する設計にしない。

### 21.10 多言語受け入れ条件

- Coreに日本語固有の品詞ID・接続ID・表記規則をハードコードしない。
- Language Packを交換して別言語の辞書・形態素体系・正書法を利用できる。
- 同一Core ABIで異なるLanguage Packをロード・検証できる。
- Pack不一致・ABI不一致・辞書破損・ライセンス情報欠落を検出し、明示エラーにする。
- 出力が漢字・かなだけに限定されず、Packの定義する文字種・数字・記号・句読点を扱える。
- 数字・単位・記号の正規化で値や意味を変えない。
- Pack変更時に既存Sessionの状態を暗黙に混在させない。
- 言語・Pack・辞書・モデル世代を診断情報と再現性キーに含める。


## 22. Language Pack分離配布と軽量パック目標（v0.6）

### 22.1 配布方針

kana-decoderは共通CoreとLanguage Packを独立した配布単位とする。Coreのインストールによって特定言語の辞書・モデルを暗黙に同梱してはならない。利用者は必要なLanguage Packだけを選択して導入できる。

- Core: 言語非依存のStreaming Engine、Branch管理、Context Core、Commit、固定予算実行、C APIを提供する。
- Language Pack: 言語固有の辞書、形態素体系、読み変換、特徴マッピング、表記・数字・記号・句読点規則を提供する。
- Optional Extension Pack: 専門語彙、高精度辞書、追加スコアラー等を必要に応じて追加する。基本Packの必須依存にしない。

CoreはPackが未導入でも生成・診断できる。変換開始時にPackがない場合は、明示的なKD_ERR_PACK_NOT_FOUND等を返す。暗黙のネットワーク取得は禁止する。

### 22.2 サイズ目標

サイズ目標は圧縮アーカイブではなく、インストール後に実際に占有するファイルサイズを基準とする。圧縮配布サイズ、展開後サイズ、ロード時RSSは別々に測定・報告する。

| 配布単位 | 初期目標 | 扱い |
|---|---:|---|
| Core runtime | 5 MB以下 | 必須 |
| Lightweight Language Pack | 30 MB以下 | 標準配布目標 |
| Core + 1 Lightweight Pack | 35 MB以下 | 標準導入セット |
| Optional Extension Pack | 個別定義 | 任意追加 |
| High-accuracy / Domain Pack | 30 MB超を許容 | 標準軽量目標の対象外 |

サイズは実装・辞書選定・バイナリ形式の検証前の目標値であり、達成済みの実測値や保証値ではない。ビルド方式、OS、アーキテクチャ、辞書ライセンスによる差分をリリースごとに記録する。

### 22.3 Lightweight Packの設計原則

軽量Packは単に大規模辞書を圧縮するのではなく、対象ユースケースに必要な情報を選別し、Coreの固定予算契約に適合させる。

1. 頻出語・基本語彙を優先し、低頻度語・専門語はOptional Extensionへ分離する。
2. 辞書エントリは必要な読み・表記・lemma・品詞属性・活用属性・接続属性・語彙コストに限定する。
3. 同一情報の重複格納を避け、共通文字列テーブル、コンパクトID、共有接頭辞Trie等を用いる。
4. 接続モデルは疎行列・圧縮表現等を評価し、実行時に全行列を展開することを必須にしない。
5. 初期スコアラーはルールベースまたは小規模な線形モデルを優先し、大規模ニューラルモデルを必須依存にしない。
6. Orthography、数字、記号、句読点の規則は小さなテーブル・ルールで表現し、辞書語彙へ重複埋め込みしない。
7. Packは読み取り専用データを基本とし、ロード後の全量コピーを避ける。
8. メモリマップまたはページ単位アクセスを実装候補とし、OS非対応環境向けにbounded read fallbackを用意する。

### 22.4 Pack内部のサイズ予算

30 MBの軽量Packに対する初期の設計配分例を示す。個別項目は実データ測定に基づき調整し、合計上限30 MBを優先する。

| 要素 | 目標予算 |
|---|---:|
| Lexicon / Reading index | 18 MB |
| Morphological attributes / connection data | 5 MB |
| Orthographic variants / normalization | 2 MB |
| Semantic feature map / lightweight scorer | 2 MB |
| Number / symbol / punctuation rules | 1 MB |
| Manifest / indexes / metadata / alignment overhead | 2 MB |
| 合計 | 30 MB |

予算超過時は、必須機能を無条件に削るのではなく、語彙の頻度選別、重複排除、圧縮、Optional Extensionへの分離を順に検討する。ライセンス条件を満たさない語彙をサイズ目標のために取り込んではならない。

### 22.5 実行時メモリとロード方式

配布サイズと実行時メモリは別の制約とする。初期目標は次のとおり。

- Core runtimeの常駐メモリ: 5 MB以下を目標とする。
- Lightweight Packの追加RSS: 50 MB以下を目標とする。
- Packロード時に辞書全体を複製しない。
- Packデータはimmutableとし、複数Session間で安全に共有できる。
- Session固有状態、Branch、Context、未確定窓はPack共有データと分離する。
- mmap利用時もページフォールトを含む初回アクセス遅延を計測する。
- メモリマップが使えない場合の読み込み方式にも上限と失敗時の明示エラーを設ける。

これらは初期目標であり、実測結果によりCore常駐分、Pack共有分、Session追加分、ピークRSSを分けて改定する。

### 22.6 Pack配布・依存関係

Packは独立したバージョン付き成果物として配布する。Manifestに以下を含める。

- pack_id / language_id / pack_version
- minimum_core_abi_version / maximum_supported_core_abi_version
- dictionary_version / model_version / format_version
- file list / file size / SHA-256
- reading_encoding_id / normalization_profile / orthography_profile
- license_manifest / upstream / copyright notices
- declared_resource_limits / measured_size metadata

Packの取得・更新は利用者またはアプリケーションによる明示操作とする。Coreはネットワークアクセス、更新確認、テレメトリを行わない。オフライン環境ではローカルファイルからインストール・検証できる。

Packの依存関係は有向非巡回でなければならず、初期仕様ではLightweight Packが別のLanguage Pack全体を依存として要求することを禁止する。共通データが必要な場合はCore ABIまたは独立したShared Resourceとして明示する。

### 22.7 Pack検証と失敗時動作

Packロード時にABI互換性、形式バージョン、必須ファイル、サイズ上限、チェックサム、Manifest、ライセンス情報の存在を検証する。検証失敗時に部分的な辞書を正常Packとして扱ってはならない。

少なくとも次のエラーを区別する。

- KD_ERR_PACK_NOT_FOUND
- KD_ERR_PACK_INVALID
- KD_ERR_PACK_ABI_MISMATCH
- KD_ERR_PACK_FORMAT_UNSUPPORTED
- KD_ERR_PACK_CHECKSUM_MISMATCH
- KD_ERR_PACK_RESOURCE_LIMIT
- KD_ERR_PACK_LICENSE_METADATA
- KD_ERR_PACK_IO

ロード失敗時は既存Sessionや他Packの状態を破壊しない。Pack切替はSessionのDrain/Resetを経由し、異なるPack世代の候補・Evidence・Context状態を混在させない。

### 22.8 軽量パック受け入れ条件

- Core単体のインストールにLanguage Packを必須としない。
- Core runtimeの配布サイズが5 MB以下であることを測定する。
- 少なくとも1つの実用Language Packが展開後30 MB以下であることを測定する。
- Coreと軽量Packの合計が展開後35 MB以下であることを測定する。
- Packを追加・削除しても他PackおよびCoreのファイルを破壊しない。
- Pack不在・破損・ABI不一致・サイズ超過を決定的に検出する。
- Packデータを複数Sessionで共有し、Session状態は独立している。
- CPUのみ・完全オフラインでPackの検証・ロード・変換が可能である。
- ロード時間、初回アクセス遅延、追加RSS、ピークRSSを測定する。
- サイズと性能の測定条件、辞書由来、ライセンス情報をリリース成果物に記録する。

### 22.9 優先実装順

1. Core / PackのABI境界とManifest schemaを確定する。
2. Packファイル形式、versioning、checksum、resource limit検証を実装する。
3. 辞書インポーターに語彙選別・重複排除・サイズレポートを追加する。
4. 軽量日本語Packの語彙・接続データを構築し、30 MB上限を検証する。
5. mmapまたはbounded read方式を比較し、複数Session共有を実装する。
6. Packロード・破損・互換性・サイズ超過テストを追加する。
7. Core/Pack別の配布物を生成し、サイズ・RSS・起動時間・変換精度を測定する。


## 23. 不確実性の三層分離

### 23.1 基本原則

変換時の不確実性を次の三層に分離する。

1. Morphological Hypothesis: 読みの分割、形態素境界、品詞、活用、接続に関する仮説。
2. Semantic Hypothesis: 同音異義語の語義、固有名詞、対象、話題、談話関係に関する仮説。
3. Orthographic Hypothesis: 同一語義・同一形態素に対する漢字、かな、カナ、送り仮名等の表記に関する仮説。

各層は独立した候補集合、スコア、確信度、状態、revisionを持つ。ある層の確定を他層の確定とみなしてはならない。例えば語義が「雨」と確定しても、表記方針が未確定なら「雨／あめ」の選択は保留できる。

### 23.2 依存関係

各仮説は入力spanと根拠Evidenceを参照し、上流仮説への依存を記録する。上流仮説が変更された場合、依存する下流仮説だけを無効化または再評価する。

- Morphological変更は、影響するSemanticおよびOrthographic仮説を再評価する。
- Semantic変更は、対応するOrthographic仮説を再評価する。
- Orthographic変更は、原則としてMorphological / Semantic仮説を変更しない。
- 依存関係のない仮説を全件再計算してはならない。

### 23.3 Commit Gate

各層に独立したCommit条件を設ける。Morphological Commitは境界・品詞・接続の安定性、Semantic Commitは候補margin・文脈Evidence・未解決Question、Orthographic Commitは表記規則・確信度・出力ポリシーを評価する。下流層の不確実性を理由に上流層の確定を不必要に遅らせない一方、上流層の変更可能性が残る場合は依存する下流出力をCommittedにしない。

## 24. 同音異義語の曖昧性保持出力

### 24.1 目的

前後文脈、形態素接続、Context Cluster、Claim / Evidence、n-gram等を用いて候補を評価しても十分な確信が得られない場合、単一候補を強制選択せず、複数候補を明示した出力を許容する。

表示例:

- `あめがふる` → `雨が降る。`（雨の根拠が十分な場合）
- `あめをかう` → `[雨/飴/other]を買う。`（文脈だけでは語義が決まらない場合）
- `はし` → `[橋/箸/端/other]`（必要な文脈が不足する場合）

上記は概念例であり、候補集合はLanguage Packの語彙と文脈により異なる。

### 24.2 Context-masked n-gram推定

同音異義語候補の評価に、対象spanをマスクした前後文脈推定を利用できる。対象語を直接含む頻度に過度適合しないよう、対象spanを `<MASK>` として扱い、左右の文脈から候補語の適合度を推定する。

- 左右両側の文脈が利用可能な場合は双方向の文脈特徴を統合する。
- ストリーム途中で右文脈が未到着の場合は左文脈のみで暫定順位を付け、後続入力到着後に再評価する。
- n-gramは形態素・品詞・語彙・談話・Cluster等のEvidenceを補完する特徴であり、単独で意味を確定しない。
- n-gramのスコアを確率として表示する場合は、検証データによる校正を必須とする。校正前は順位スコアとしてのみ扱う。
- 学習・集計データの不足、ドメイン差、未知語に対しては不確実性ペナルティを加える。

### 24.3 候補集合とother

曖昧性出力は、少なくとも次の情報を内部表現として持つ。

- target_span: 元入力に対応する読み範囲
- candidate_items: 表記候補、語彙ID、形態素・語義ID（存在する場合）
- other_mass: 登録候補以外の語義・未知語が残る度合い
- evidence_summary: 左右文脈、n-gram、形態素、Cluster等の根拠
- confidence_state: high / medium / low / uncalibrated
- revision / lifecycle_state

`other` は辞書に存在する具体的な単語ではなく、候補集合に含まれない未知語・固有名詞・未登録語義等を許容するためのオープンセット候補である。候補集合を固定語彙だけに閉じてはならない。

### 24.4 出力ポリシー

出力モードを次のように定義する。

- Normal: 十分な確信があれば単一表記を出力する。
- Ambiguous: 確信度または候補marginが閾値を満たさない場合、`[雨/飴/other]` 形式等で複数候補を出力する。
- Conservative: 曖昧性表記を許可しない呼び出し元向けに、かな表記へフォールバックする。
- Diagnostic: 候補ID、順位スコア、Evidence、閾値判定を構造化情報として返す。

Ambiguous出力は通常の本文と機械可読な候補情報を区別できるようにする。角括弧形式は既定の人間可読表現とし、APIでは専用のAmbiguity Segmentとして表現する。利用者が入力した角括弧とシステム生成の曖昧性表記をprovenanceで区別する。

### 24.5 判定条件

単一候補への確定は、絶対スコアだけでなく、候補間margin、Evidenceの独立性、n-gram校正状態、other_mass、ドメイン適合性を考慮する。閾値はLanguage Pack・ドメイン・用途別に設定可能とする。誤った断定のコストが高い場合は曖昧性保持を優先する。

## 25. 軽度の言い間違い・文字起こし誤り補正

### 25.1 目的と適用範囲

入力かな列に含まれる軽度の誤りについて、元入力を保持したまま補正仮説を生成し、通常の読み候補と並行評価する。対象は小規模な読み・文字列の乱れであり、自由な文章書き換えや意味の推測による大幅な修正は行わない。

対象例:
- 音の近接・混同: 清音/濁音、拗音、長音、促音等の近接誤り
- 文字の脱落・重複・挿入
- 音声認識結果における局所的な読み違い
- 発話時の軽度な言い間違い・言い直し候補
- 文脈と形態素接続から強く疑われる局所的な読み誤り

テキストだけから、話者の言い間違いとASRの誤認識を確実に区別することはできない。したがってCorrection Engineは原因を断定せず、Correction Hypothesisとして扱う。音声側から信頼度や代替認識候補が渡された場合は、任意Evidenceとして利用できる。

### 25.2 原文保持と二重仮説

Correction Engineは入力原文を不変のsourceとして保持し、補正後の読みを別の仮説Branchとして生成する。

各Correction Hypothesisは以下を持つ。

- source_span / source_reading
- proposed_reading
- edit_operations
- correction_type
- language_pack_rule_id
- context_evidence
- acoustic_evidence（任意）
- correction_score
- confidence_state
- revision / lifecycle_state

原文を破壊的に上書きしてはならない。補正候補の採用・撤回はrevision付き差分として通知する。診断モードでは原文、補正案、補正理由を確認可能にする。

### 25.3 補正候補の生成

Language Packは言語固有の読み誤り規則・混同表・編集操作コストを提供できる。Coreは固定予算内で候補を生成・管理する。

初期実装の候補操作:
- 1文字の挿入・削除・置換
- 連続文字の重複・脱落
- 事前定義された音韻近接・混同ペアの置換
- 長音・促音・拗音に関する限定的な変換

複数編集の組合せは探索上限を設け、通常は小さい編集距離に限定する。候補生成数、編集距離、対象span長、再評価回数に固定上限を設ける。任意の長文書き換えや辞書全件との無制限照合は禁止する。

### 25.4 補正候補の評価

補正候補は次の独立Evidenceで評価する。

- 読み・形態素としての成立性
- 前後文脈・述語・格関係との整合性
- Context Cluster / Claimとの適合性
- n-gram文脈尤度
- Language Packの混同規則と編集コスト
- 音声由来の信頼度・代替候補（提供された場合）
- 原文候補に対する相対的な改善幅

文脈に合うという理由だけで原文を補正してはならない。補正コストを常に課し、原文候補より明確に優位で、かつ補正信頼度が用途別閾値を超える場合に限り自動採用を許容する。低信頼の場合は原文を維持し、必要なら補正候補を曖昧性情報または診断情報として返す。

### 25.5 安全制約

- 数字、日付、時刻、金額、単位、URL、識別子、固有名詞は保護対象として扱い、明示的なEvidenceなしに補正しない。
- 意味を変える可能性のある置換は高い閾値を要求する。
- 明示的な言い直し（例:「A、いやB」）はCorrection Hypothesisではなく発話イベントとして扱い、Packの談話規則に従う。
- 入力に存在しない内容を新規生成しない。
- 自動補正を無効化できる設定を提供する。
- 原文・補正後読み・最終表記の対応関係を追跡可能にする。

### 25.6 出力とCommit

補正候補はMorphological Hypothesisの代替読みとして管理し、その後Semantic / Orthographic層で独立評価する。補正が未確定のままの場合は原文読みを保持し、必要に応じて候補を提示する。補正を含む範囲は通常の形態素・意味Commit条件に加え、Correction Gateを満たすまで確定しない。Committed後の自動補正は禁止し、変更には明示的なreopen操作を要求する。

## 26. 受け入れテスト追加

### 26.1 曖昧性保持

1. `あめをかう` のように複数の語義が成立し、左右文脈・形態素・n-gramでもmarginが不足する場合、`[雨/飴/other]` 等の候補出力またはConservativeモードのかな表記を返す。
2. `あめがふる` のように述語が語義を強く限定する場合、不要な曖昧性表示を避ける。
3. 右文脈が未到着の時点では暫定候補を保持し、後続入力で曖昧性出力を解消できる。
4. 未知語・固有名詞が候補にない場合、otherを残し、既知候補へ無理に押し込まない。
5. 人間可読な角括弧表示とAPIのAmbiguity Segmentを区別し、ユーザー入力の角括弧を誤認しない。

### 26.2 軽度誤り補正

1. 1文字の脱落・重複・近接音誤りに対し、原文と補正案を別Branchとして保持する。
2. 文脈が補正案を強く支持しない場合、原文を維持する。
3. 文脈・形態素・補正規則が補正案を強く支持する場合のみ、設定された閾値に従い自動補正できる。
4. 数字・単位・固有名詞・識別子を誤って変更しない。
5. 後続入力で補正案の優位性が失われた場合、未確定範囲で補正を撤回する。
6. Correction Engine無効時は入力読みをそのまま処理する。
7. ASR由来Evidenceがない場合、言い間違いと認識誤りの原因分類を断定しない。

### 26.3 性能・資源

- 曖昧性候補数、n-gram参照数、補正編集距離、補正候補生成数に固定上限を設ける。
- 通常変換に対する追加レイテンシ、候補数、処理ステップ、peak RSSを個別計測する。
- n-gram・補正機能が無効または候補なしの場合に通常経路へ余計な全探索を持ち込まない。
- 既存の1文節1 ms未満目標は、機能追加後に実測して再評価する。達成を事前に保証しない。

---
v0.7は曖昧性の明示出力と軽度誤り補正を追加した設計仕様であり、閾値・校正・精度・性能は実装および評価データによる検証後に確定する。
