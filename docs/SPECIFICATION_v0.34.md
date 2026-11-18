# kana-decoder 仕様書 v0.34

- Version: 0.34.0
- Core license: MIT
- Runtime: CPU-only / offline
- Focus: offline SURGE lexical context-cluster feasibility evaluation with distance-preserving causal evidence.

## 1. Purpose

v0.31 connected the native SURGE evidence gate to full-path C++ sentence decoding. The first Sudachi Pack measurement showed that the default margin of 0.5 produced exactly the morphology-only result. This indicates that the conservative gate did not activate often enough to affect the held-out corpus. v0.32 adds a reproducible calibration phase using only the development split.

## 2. Calibration policy

The native sequential evaluator supports two explicit modes:

- `dev-calibrate`: reads only rows whose split is `dev`, evaluates a bounded parameter grid, and writes the selected configuration to a transient TSV file.
- `test`: reads only rows whose split is `test`, loads the frozen calibration TSV, and evaluates the selected configuration once.

The dev grid contains 72 configurations:

- Context weight: 0.25, 0.5, 1.0, 2.0.
- Evidence margin: 0.0, 0.01, 0.05, 0.1, 0.25, 0.5.
- Additive smoothing alpha: 0.1, 0.5, 1.0.

Selection is deterministic: lowest Unicode-codepoint CER first, then higher sentence exact accuracy, then higher exact token alignment. If no calibrated SURGE configuration beats morphology-only on dev, the emitted configuration disables contextual contribution (weight 0.0) and uses the safe default alpha/margin. This avoids deploying a dev configuration that degrades the morphology baseline.

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
- Exactly 72 calibration combinations are evaluated.
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
