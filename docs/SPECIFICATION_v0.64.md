# kana-decoder: Pinned real-corpus SURGE runtime evaluation v0.64

Version: 0.64.0
Parent: SPECIFICATION_v0.63.md

## 1. Purpose

Run the current SURGE Core normalization runtime, not only the legacy sequential/cluster evaluator, against the pinned Sudachi Small dictionary and UD Japanese-GSD corpus. Produce comparable candidate-generation, token-aligned recall, miss-attribution, SURGE selection, normalization, elapsed-time and peak-RSS measurements.

## 2. Data and split integrity

- Sudachi Small source and license are pinned by `licenses/sudachidict-small-20260723-source-lock.json`.
- UD Japanese-GSD source revision is pinned by `licenses/ud-japanese-gsd-source-lock.json` and checked against the generated corpus manifest.
- The runtime evaluation source-lock artifact combines both source locks, the UD corpus manifest and the compiled document-disjoint split manifest.
- Training sequences are built from train only.
- The eight-policy calibration runs only on dev. Policy selection remains minimum dev CER, then maximum dev sentence exact, then fixed preset order.
- The selected policy is applied once to test. Test metrics are never fed back into calibration.
- Six-column tokenized corpus rows supply trusted token boundaries for token-aligned recall and miss attribution. Sentence-only inputs must continue to report null token recall.

## 3. Runtime evaluation

The workflow builds `kana_decoder_surge_eval` from the current branch and invokes it on the pinned corpus:
- dev calibration: `tools/calibrate_surge_policy.py`, using the six-column tokenized corpus and train-only sequences.
- held-out test: `kana_decoder_surge_eval ... test TRAIN_SEQUENCES SELECTED_POLICY`.
- The JSON reports sentence-level raw-surface Candidate Recall@1/@5/@10, oracle CER@5/@10, conditional SURGE exact success, token-aligned Recall@1/@5/@10, four-way token miss attribution, candidate error partitions, SURGE CER/exact/defer/failure counts, typed-entity evidence counters, elapsed time, throughput and peak RSS.

## 4. Resource gate and artifacts

- Every dev evaluator subprocess must report a peak RSS not exceeding 1,792 MiB. Missing RSS fails calibration closed.
- The held-out test evaluator is wrapped with `/usr/bin/time -v` and checked against the same 1,792 MiB ceiling.
- Upload only the source-lock, selected policy and aggregate JSON/time reports as the diagnostic artifact. Do not upload the private runtime corpus or raw sentence-level traces.
- The separately verified Language Pack artifact remains available under its existing artifact policy.

## 5. Interpretation limits

- Candidate recall measures whether the gold surface is exposed by the raw N-best decoder paths, not whether SURGE selects it.
- Miss classes are diagnostic signals, not causal proof. Same-span surface mismatch, same-surface span mismatch and missing surface have different likely remedies, but require case review before code changes.
- The pinned UD corpus is a morphology/context benchmark, not a complete conversational Japanese corpus. Its CER is not a universal product-quality claim.
- Results from the legacy sequential/cluster evaluator are retained as a separate baseline and must not be mixed with this SURGE Runtime report.
- No target business-PC latency or memory claim is made from hosted CI measurements.

## 6. Completion gate

The workflow change is implemented when the branch CI and pinned corpus workflow both complete successfully. Product conclusions require inspection of the dev miss distribution and a single frozen test report. Any subsequent tuning must use dev only and trigger a new versioned evaluation with an unchanged test set.
