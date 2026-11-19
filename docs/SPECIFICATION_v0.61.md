# kana-decoder: Candidate error attribution diagnostics v0.61

Version: 0.61.0
Parent: SPECIFICATION_v0.60.md

## 1. Purpose

Extend sentence-level N-best candidate coverage metrics with an explicit accounting of where covered and uncovered cases fail. The goal is to distinguish missing candidate coverage from downstream SURGE runtime, deferral, selection, and surface-realization outcomes without claiming token-level morphology alignment.

## 2. Error classes

The evaluator emits `candidate_error_classification` with the following counters:

- `missing_candidate`: the exact gold sentence surface is absent from the first 10 raw decoder paths. Decoder failures are included.
- `candidate_available`: the exact gold sentence surface is present in the first 10 raw paths.
- `candidate_available_runtime_failure`: candidate coverage exists, but SURGE runtime initialization fails.
- `candidate_available_deferred`: candidate coverage exists, but normalization does not commit.
- `candidate_available_surge_exact`: candidate coverage exists and final SURGE output exactly matches the reference.
- `candidate_available_surge_wrong`: candidate coverage exists, normalization commits, but final output is not exact.
- `wrong_output_matches_raw_nbest`: within the preceding wrong-output class, final output equals one of the raw N-best surface strings. This is consistent with a wrong candidate/path choice.
- `wrong_output_outside_raw_nbest`: within the preceding wrong-output class, final output equals none of the raw N-best surface strings. This identifies a surface divergence introduced downstream, but is not by itself proof of a renderer defect.

## 3. Accounting invariants

For every evaluation set:

`missing_candidate + candidate_available = cases`

`candidate_available_runtime_failure + candidate_available_deferred + candidate_available_surge_exact + candidate_available_surge_wrong = candidate_available`

`wrong_output_matches_raw_nbest + wrong_output_outside_raw_nbest = candidate_available_surge_wrong`

The black-box CTest contract verifies these identities and cross-checks candidate_available against exact sentence Recall@10. The dev calibrator copies the classification object into every candidate row for diagnosis only; it does not use these counters to select policy weights.

## 4. Interpretation and limitations

- High `missing_candidate` points toward dictionary coverage, segmentation, or search-width limitations.
- High `candidate_available_surge_wrong` points toward ranking, evidence weighting, Focus/context, or surface realization.
- High `wrong_output_matches_raw_nbest` suggests a wrong raw path was selected.
- High `wrong_output_outside_raw_nbest` indicates the final surface differs from every raw N-best surface. Inspect typed-entity transformations and renderer span alignment before assigning blame.
- These counters operate on sentence-level exact raw surface coverage. They are not token-aligned morphological recall and do not establish causal attribution on their own.

No real-corpus accuracy gain, held-out quality improvement, or target-PC resource qualification is claimed until the pinned real-data workflow and target-device measurements complete.

## 5. Verification

- Extend the real evaluator black-box CTest to verify error-class partition identities.
- Extend the dev calibration unit test to verify classification propagation.
- Run final-head CI before treating this stage as verified.
