# kana-decoder Specification v0.98: Quality-linked bottleneck attribution

## Scope

This revision joins the paired SURGE-versus-morphology quality deltas from v0.96 with the mutually exclusive candidate-loss diagnostics from v0.97. It does not alter decoding or policy selection.

## New report fields

- `surge_bottleneck_by_domain`
- `surge_bottleneck_by_reading_length`

Each group combines:
- `cer_delta`: SURGE CER minus morphology CER; negative is favorable
- `exact_accuracy_delta`: SURGE exact accuracy minus morphology exact accuracy; positive is favorable
- `token_alignment_recall_delta`: SURGE token alignment recall minus morphology recall; positive is favorable
- `dominant_loss_stage`, `dominant_loss_tokens`, `dominant_loss_pct`
- `gold_tokens` and `loss_stage_rates_pct`

A combined group is emitted only when its quality-comparison and candidate-survival strata both exist. The group keys are checked against the intersection of the two source maps. The quality deltas are copied from the paired comparison calculation, while loss attribution is derived from the existing mutually exclusive candidate counters.

## Interpretation and safety

The combined report supports diagnosis, not causal proof. A large loss stage and a negative CER delta in the same stratum are correlated observations; they do not prove that changing that stage will improve quality. Candidate-generation or ranking changes must be tested as controlled ablations and selected using development data only. Held-out test data remains report-only.

## Integrity tests

Tests verify group-key intersections, equality with the source quality deltas, equality with candidate-loss attribution, and the 100-percent partition of loss-stage rates for nonempty groups.

## Runtime and memory

This is post-decode aggregation over existing maps and counters. It introduces no additional decoding, retains no sentence-level output, and does not change production inference. The 100 MiB bound remains limited to the bounded decode microbenchmark, not the full Japanese Pack.
