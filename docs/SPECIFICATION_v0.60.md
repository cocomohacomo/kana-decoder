# kana-decoder: N-best candidate recall and SURGE selection diagnostics v0.60

Version: 0.60.0
Parent: SPECIFICATION_v0.59.md

## 1. Purpose

Separate candidate-generation limitations from downstream SURGE selection and surface realization errors. The real-data evaluator measures whether the gold sentence surface exists among the decoder's raw N-best paths before SURGE normalization.

## 2. Candidate-generation metrics

For each input case, concatenate candidate token surfaces for each returned N-best path and compare the result with the gold sentence by Unicode code-point edit distance.

Report:
- mean returned N-best path count per case;
- sentence-level raw-surface exact Candidate Recall@1, @5 and @10;
- oracle CER@5 and @10, using the minimum edit distance among the first N candidate paths;
- conditional SURGE exact success among cases where the gold surface exists in the top 10 raw candidates.

The denominator is the complete evaluation set. A decoder failure is a candidate miss and contributes the gold reference length as deletion distance to both oracle CER aggregates. Recall@N must be monotonic: Recall@1 <= Recall@5 <= Recall@10. Oracle CER must be non-increasing as N grows.

The conditional SURGE metric is the fraction of top-10-covered cases for which the final SURGE output is also an exact match. Runtime failure, deferral or incorrect realization counts as a miss in this conditional rate.

These are sentence-level raw-surface coverage diagnostics. They are not token-aligned morphological Candidate Recall, do not normalize entities, and do not measure final SURGE output accuracy.

## 3. Calibration integration

The dev-only policy calibrator carries the candidate-generation metric object into each candidate row. It does not use candidate metrics or test data to select a policy; selection remains minimum dev SURGE CER, then maximum dev sentence exact, then fixed preset order.

## 4. Verification

A black-box test invokes the real evaluator with the Japanese reference Pack and smoke corpus, validates the output schema, verifies recall monotonicity and verifies oracle-CER monotonicity. CTest registers this test when Python is available.

## 5. Interpretation

- Low Recall@10 indicates candidate generation, dictionary coverage, segmentation, or search-width limitations. SURGE cannot select a correct path that does not exist.
- High Recall@10 with low SURGE accuracy indicates ranking, evidence weighting, focus/context, or renderer limitations.
- High candidate oracle coverage with poor normalized output should be investigated by comparing the selected path, entity spans, renderer alignment and final output.

No real-corpus accuracy gain, held-out Candidate Recall@N, target-PC latency or Peak RSS improvement is claimed until the pinned real-data workflow completes.
