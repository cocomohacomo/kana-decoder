# kana-decoder 仕様書 v0.40

- Version: 0.40.0
- Core license: MIT
- Runtime: CPU-only / offline
- Focus: offline SURGE lexical context-cluster feasibility evaluation with distance-preserving causal evidence.

## 1. Purpose

v0.31 connected the native SURGE evidence gate to full-path C++ sentence decoding. The first Sudachi Pack measurement showed that the default margin of 0.5 produced exactly the morphology-only result. This indicates that the conservative gate did not activate often enough to affect the held-out corpus. v0.32 adds a reproducible calibration phase using only the development split.

## 2. Calibration policy

The native sequential evaluator supports two explicit modes:

- `dev-calibrate`: reads only rows whose split is `dev`, evaluates a bounded parameter grid, and writes the selected configuration to a transient TSV file.
- `test`: reads only rows whose split is `test`, loads the frozen calibration TSV, and evaluates the selected configuration once.

When the experimental SURGE cluster sidecar is available, the joint grid evaluates 144 configurations:

- Word N-gram context weight: 0.0, 1.0, 2.0.
- Evidence margin: 0.0, 0.1, 0.5.
- Additive smoothing alpha: 0.1, 0.5.
- POS N-gram weight: 0.0, 0.15.
- SURGE cluster weight: 0.0, 0.5, 1.0, 2.0.

Without a cluster sidecar, cluster weight is fixed at zero and only 36 distinct configurations are evaluated. The evaluator does not repeat equivalent configurations merely to preserve a nominal grid size.

Selection is deterministic: lowest Unicode-codepoint CER first, then higher sentence exact accuracy, then higher exact token alignment. If no calibrated configuration beats morphology-only on dev, the emitted configuration disables contextual contribution and uses the safe default alpha/margin. The test split is never used for selection.

## 3. Smoothing API

`MaskedNgramScorer` now exposes:

- `set_smoothing_alpha(double)`: accepts finite values greater than 0 and at most 100; rejects invalid values without changing the current value.
- `smoothing_alpha()`: returns the active alpha.

The default remains 0.5, preserving existing scoring behavior. The selected alpha is applied to both unigram and contextual additive smoothing for the calibrated evaluation. No trained counts, calibration values, or new model files are embedded into the Language Pack.

## 4. Evaluation boundaries

- Training n-gram counts are generated from the train split only.
- Dev calibration uses gold tokenized rows only for sentence construction and post-decode alignment. Gold token boundaries are never supplied to the decoder.
- Test rows are not read during parameter selection. The selected configuration is frozen to a transient TSV before the test invocation.
- Test evaluation reports morphology-only, ordinary train-derived unigram + predicted-left context, and calibrated SURGE-gated left-context.
- Future/right context is not supplied to the native sequential decoder.
- The test result is not used to update the selected configuration.

This is full sentence reading-to-text decoding with gold sentence/token references for scoring, but it is not raw unsegmented-document evaluation or a live streaming latency benchmark.

## 5. Resource and compatibility constraints

- Candidate shortlist <=8, beam width <=8, expanded states <=262,144.
- Calibration reuses the same bounded decoder and scorer; it does not retain per-configuration lattices or paths between runs.
- Runtime default smoothing alpha remains 0.5; SURGE gate remains opt-in.
- No C ABI or Language Pack schema change.
- Calibration TSV and corpus data exist only in the CI workspace and are not committed or uploaded.

## 6. Validation requirements

CI must verify:
- Valid and invalid smoothing alpha handling.
- Exactly 144 joint combinations are evaluated when the cluster sidecar is present, or 36 distinct combinations without it.
- Dev mode selects only dev rows and writes a readable configuration.
- Test mode reads the frozen configuration and scores only test rows.
- The full Sudachi Pack workflow measures calibration and test Peak RSS separately, each under 1,792 MiB.

No v0.32 accuracy gain is claimed until the new CI and full Pack workflow complete.


## 7. v0.33 事前言語クラスターの実現可能性検証

- train splitの単語系列から、各語の前後2語以内の文脈プロファイルを作成する。
- TF-IDF重み付き疎ベクトルのコサイン類似度から、決定論的Union-Findで語彙クラスターを生成する。語彙上限2,000、クラスター上限24語、共通文脈の過剰寄与を抑制する。
- クラスターごとに文中の候補出現統計を保持し、推論時は過去に予測した候補のクラスターだけを活性化する。候補のクラスター証拠が不足する場合は形態素順位へフォールバックする。
- 証拠スコア差の単一gap thresholdをdev splitのみで校正し、凍結した設定でtest splitを一度評価する。クラスター生成はtrain splitのみ。
- tools/evaluate_surge_clusters.py は軽量なSURGEクラスター仮説の実現可能性プローブであり、SURGE Core本体や本番C++デコーダへの統合ではない。
- 現段階の評価はgold token境界・読みを使う候補選択診断であり、未知の文節境界を含む完全なかな入力からのCERではない。
- 成績が改善しない場合は本番統合しない。test結果でパラメータを再調整しない。


## 8. v0.34 距離保持型クラスター証拠

- v0.33の文単位bag-of-words集計を廃止し、train系列内の先行トークンのクラスターと後続候補IDを距離別に集計する。
- 推論時は予測済み左履歴の直近2候補のみを参照し、距離1・距離2の統計を混同しない。
- 文全体の話題共起を局所的な変換根拠として誤用しない。未知クラスター、未観測距離、証拠不足は解決せず形態素順位へフォールバックする。
- クラスター生成・距離別統計はtrainのみ、beta/margin校正はdevのみ、testは凍結設定で一度評価する。
- 本変更は評価プローブ内の改善であり、SURGE Core統合や本番デコーダの精度向上を意味しない。
- テストでは距離2の証拠が距離1の証拠として誤利用されないこと、決定性、クラスターサイズ上限、既存の合成曖昧性解決を検証する。


### v0.34.1 評価上のQuestion定義

- Questionは候補集合に異なる表層候補が2つ以上存在する場合に限り生成する。
- 表層候補が1種類しかないトークンはQuestionでもabstentionでもない。解決率・棄権率の分母から除外する。
- 指標に question_tokens を追加し、resolved_tokens + abstained_tokens = question_tokens を不変条件とする。
- この変更は評価カウントの意味を正すものであり、デコーダ出力や候補スコアを変更しない。


### v0.34.2 校正パラメータの単純化

- 旧beta × margin条件は gap >= margin / beta と等価で、独立パラメータではなかった。
- 校正を単一の gap_threshold（0.0, 0.01, 0.05, 0.1, 0.25, 0.5, 1.0）へ変更し、dev上で直接選択する。
- testには選択済みthresholdを固定して適用する。重複した実質同一設定による校正グリッドの水増しを防止する。


## 9. v0.35 実験用SURGEクラスタモデルのシリアライズ

- v0.34の実データ候補選択診断で、形態素ベースライン45.2936%に対してクラスタ方式53.3047%（+8.0110ポイント）を確認した。評価対象は候補が存在した9,774トークン。解決1,165件、解決精度87.5536%、正解解決再現率10.4359%。
- 上記はゴールドのトークン境界・読みを使う候補選択診断であり、全文かな入力のCERや本番デコーダの精度改善を意味しない。
- tools/compile_surge_clusters.py を追加し、trainのみから作成したtoken-cluster割当、距離1/2別候補証拠、クラスタ合計、devで選択したgap thresholdをバージョン付きTSVへ出力する。
- 形式ヘッダは KANA_SURGE_CLUSTER<TAB>1。META / TOKEN / EVIDENCE / TOTALレコードを持ち、順序は決定的とする。SHA-256、バイト数、レコード数を含むmanifestを別ファイルに出力する。
- ハード上限: モデル64 MiB、クラスタ4,096、token membership 100,000、evidence 500,000、ID 128 UTF-8 bytes、count uint32、距離1または2。上限・不正ID・不正距離・非有限閾値はfail-closed。
- モデルはLanguage Packへまだ同梱せず、C++ runtimeからも読み込まない。従来デコーダの挙動・ABI・既存Pack互換性を維持したまま、次段階のC++ loader/scorer検証に使うsidecarとする。
- CIではSudachi実Packの評価と同じtrain/dev/test分割からsidecarを生成し、manifestを出力する。test splitは閾値校正に使わない。


## 10. v0.36 C++ SURGEクラスタ統合（実験機能）

- `SurgeClusterModel` を追加し、sidecar v1のMETA / TOKEN / EVIDENCE / TOTALをC++で読み込む。最大64 MiB、cluster 4,096、membership 100,000、evidence 500,000、ID 128 bytes、距離1/2を強制し、不正・重複・参照欠落はfail-closedとする。
- Python probeと同じ履歴方向・距離別count・加算平滑化（sidecar指定alpha）・membership数×4の語彙近似でEvidence scoreを計算する。
- DecoderOptionsに任意のSURGE cluster model pointerを追加。既定nullptrのため既存APIのデフォルト挙動とLanguage Pack形式は維持。
- 各Beam状態の予測済み直近2候補からクラスタを活性化し、形態素上位4候補とクラスタEvidence上位候補を最大8候補の範囲で統合する。Questionに対するクラスタgapがsidecar threshold以上なら、勝者表層を局所形態素最良候補に対して優先できるだけの相対bonusを付与する。
- 既存N-gram SURGE gateは負のlog probabilityを未観測候補の0点と直接比較していた。候補間gapを算出し、解決時は勝者表層に正の相対bonusを付与する方式へ修正。
- sequential evaluatorはsidecarを指定した場合にSURGE Cluster単独とN-gram + SURGE Clusterを別々に出力する。test側のsidecarはtrain生成/dev校正済みで固定し、testで再校正しない。
- sidecarはLanguage Packへ同梱せず、既定decoderでも無効。実データ全文評価のCER改善が確認されるまでは実験機能として扱う。
- C++ loader/scorerの単体テスト、候補上位4から漏れたクラスタ支持候補の復帰テスト、Python/C++実データ比較をCIに追加。


## 11. v0.37 SURGE経験記憶の多次数N-gram統合

- N-gramをSURGEの経験Evidenceとして統合し、train系列からUnigram、Bigram、Trigram、Skip-Bigram（直前2位置の語と現在語の関係）を構築する。
- 推論ではBeamごとの予測済み履歴のみを参照し、未来情報を利用しない。Trigram / Bigram / Skip-Bigramの観測Evidenceを個別評価し、観測された次数だけを重み付き平均する。初期重みはTrigram 0.60、Bigram 0.30、Skip-Bigram 0.10。
- Unigramは基礎頻度として保持し、SURGE Cluster Evidenceと同時に利用できる。既存の形態素接続行列は品詞・接続情報の別Evidenceとして維持する。
- Skip-Bigramは距離2関係を独立保持し、隣接Bigramと混同しない。未観測チャネルは平均の分母から除外し、0点の肯定証拠として扱わない。
- 既定DecoderOptions、Language Pack形式、C ABIは変更しない。係数は初期値でありdevで校正し、testは凍結設定で一度評価する。
- Character N-gramは同一読みスパンの候補間で入力文字列が共通となるため、候補スコアへ安易に加算しない。未知語候補生成・読み誤り検出の別タスクとして扱う。


## 12. v0.38 可変長N-gram履歴

- SURGE経験記憶の履歴上限を3候補へ拡張し、Unigram / Bigram / Trigram / 4-gram / Skip-Bigramを同時評価する。
- 推論ではBeamごとの予測済み履歴のみを参照し、4-gram 0.50、Trigram 0.30、Bigram 0.15、Skip-Bigram 0.05を初期重みとして、観測された次数だけで重みを再正規化する。
- Beamごとに直近3候補IDを保持し、分岐間で履歴を共有しない。Beam幅8・入力256 codepoints・展開262,144状態の既存ハード上限は維持する。
- 重みは初期値でありdev splitで校正する。testデータを使った重み調整は禁止する。


## 13. 品詞N-gram経験記憶（v0.39）

- Language Packの辞書候補IDから品詞ラベルを取得し、train splitの候補ID系列と同じ境界で品詞系列を生成する。
- 品詞系列にもUnigram / Bigram / Trigram / 4-gram / Skip-Bigramを構築し、各Beam状態が候補ID履歴と品詞履歴を独立保持する。
- 候補の品詞N-gramスコアをSURGE経験Evidenceとして加算する。品詞ラベルが欠落・競合する候補は品詞Evidenceを適用しない。
- Cluster-only評価では品詞N-gramを無効にし、N-gramおよびcombined評価では有効にして寄与を分離する。
- 既存の接続コスト行列と二重計上する可能性があるため、品詞N-gramの重みはdevで校正し、実testで改善が確認されるまでは既定デコーダで無効。


## 14. v0.39 統合認知Evidence

- Word N-gramとPOS N-gramを別の経験記憶として学習し、候補ID履歴と品詞履歴をBeamごとに分離保持する。
- POS系列は辞書候補IDから一意に解決できる品詞だけを採用する。候補IDの品詞が競合する場合はそのIDのPOS Evidenceを生成しない。
- 候補ID→品詞索引は明示的なbuild_candidate_pos_index()呼び出し時だけ生成する。通常の辞書ロード・デコードでは索引を構築せず、追加メモリを消費しない。
- POS N-gramの寄与はdev splitで0.0 / 0.15から校正し、SURGEの文脈ゲート・word N-gram smoothingと合わせて評価する。sidecarありの総数は144構成、なしは36構成。
- Cluster-only評価ではword N-gramとPOS N-gramを無効化し、N-gram-only / cluster-only / combinedの寄与を分離する。
- POS N-gramは接続行列と情報が相関する可能性がある。devで選ばれた重みを固定し、test結果を使った再調整は禁止する。
- 現段階ではN-gram学習モデルはCI評価時にtrain splitから構築し、Language Packへ同梱しない。製品配布用モデルの圧縮・更新方式は別途検証する。


## 15. v0.40 Evidence競合制御と統合校正

- dev校正時にWord N-gram / POS N-gram / SURGE Clusterの寄与を同時に探索し、Morphology・N-gram単独・Cluster単独・Combinedを同一dev splitで比較する。
- 校正値はcontext weight {0,1,2}、margin {0,0.1,0.5}、alpha {0.1,0.5}、POS weight {0,0.15}、Cluster weight {0,0.5,1,2}の144構成（sidecarなしはCluster weight=0固定の36構成）。
- Clusterモデルをdev-calibrateにも明示的に渡し、Cluster weight=0で無効化、>0で有効化する。test時は凍結した5値校正ファイルを使用。
- devで形態素ベースラインを超えない場合は全Evidence weightを0に戻す。testは一切校正に使用しない。
- testレポートはMorphology / N-gram / Cluster / dev選択Combinedを分離表示する。


## 11. v0.40.1 校正グリッドの重複排除

- SURGEクラスタsidecarがない場合、クラスタ重みだけが異なる設定は同一条件となるため、クラスタ重みを0に固定して36設定のみを評価する。
- sidecarがある場合は従来どおり144設定を評価し、Word N-gram / margin / smoothing / POS / cluster weightをdev split上で同時校正する。
- これにより、クラスタ機能なしのCTestで同一デコードを4回繰り返す無駄を除き、評価件数を実際のユニーク設定数と一致させる。
- 推論アルゴリズム、Language Pack、デフォルト動作、メモリ上限は変更しない。


## 16. v0.40.2 POS Evidenceの純化

- MorphologyDecoderのPOS候補スコアを `MaskedNgramScorer::score()` から `contextual_score()` に変更する。
- `score()` はPOS unigram priorとcontextual scoreの和である。形態素候補はすでに辞書コストで語彙priorを持つため、POS unigramを追加すると同種の頻度priorを二重計上する可能性がある。
- POS Evidenceは直前の予測POS履歴に対する観測済み文脈Evidenceだけを加点し、履歴がない場合や未観測の場合は加点しない。
- POS unigramが強くても前POSとの観測bigramが反対候補を支持する合成回帰テストを追加する。
- 通常設定ではPOS scorer pointerがnullのため、既定の推論挙動とLanguage Packは変更しない。


## v0.40.3 正解候補生存診断

- C++逐次評価器に、gold tokenをデコード処理へ渡さず、デコード完了後の診断にのみ使う候補生存追跡を追加。
- 各gold tokenについて、辞書ラティスに正解表層が存在するか、形態素コスト上位8候補に残るか、Beam展開時のCandidateScoreTraceに現れるか、最終best pathで正解tokenとして整列したかを集計する。
- 脱落分類はラティス未収録、形態素top-8選抜、展開/Beam到達、最終選択に加え、trace上限到達による判定不能を区別する。クラスタEvidenceによる候補追加はscored/expanded側へ現れるため、top-8より後段での救済も観測できる。
- 診断はtest splitの凍結済みdev選択policyで一度だけ実行し、dev校正の各候補構成では実行しない。通常decoder API、候補選択、Language Pack形式は変更しない。
- CandidateScoreTraceは既存の最大4096件/文の上限を使用。ラティス自体も既存のedge/candidate上限下で生成される。
- この診断値は原因切り分け用であり、正解情報を用いた候補ランキング性能の値を通常推論精度と混同しない。


## v0.40.4 N-best回復可能性の分離診断

- v0.40.3の `final_selection_drop` は、正解候補が最良パスに含まれないことを示すが、N-best内の別パスに残っているケースと、探索・最終N-best形成の段階で消失したケースを区別できなかった。
- test splitの凍結済みpolicyで返された全N-best path（最大8件）をdecode完了後に走査し、正解tokenのspan/surfaceが1件以上のpathに含まれるかを集計する。
- `nbest_aligned` はいずれかのN-best pathに存在する正解token数、`nbest_recoverable` はN-bestには存在するがbest pathには存在しない数、`scored_not_in_nbest` はCandidateScoreTraceで展開が確認されたが返却N-best pathには残らない数とする。
- `nbest_aligned = final_aligned + nbest_recoverable` を必須不変条件としてテストする。
- `scored_not_in_nbest` はBeam枝刈りだけでなく、返却path上限や異なるtoken segmentationの影響も含み得るため、直ちにBeam pruning原因と断定しない。詳細な原因分離は次段階のpath-level traceで行う。
- 診断はdecode完了後に行い、gold tokenを候補生成・スコアリング・Beam選択へ渡さない。通常デコード、Pack形式、ABI、メモリ上限は変更しない。
