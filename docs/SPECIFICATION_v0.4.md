# kana-decoder 仕様書 v0.4

- Status: Design specification
- Version: 0.4.0
- Core: C++ / public ABI: C API
- Runtime: CPU only / fully offline
- Primary latency target: 1 ms未満 / 文節（定常状態）
- Core characteristic: Streaming-Native Contextual Kana-Kanji Decoding with Branch-based Candidate Management and Bounded-Work Execution

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

