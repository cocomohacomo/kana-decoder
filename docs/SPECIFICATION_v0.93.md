# kana-decoder Specification v0.93: Enforced real-corpus stratified diagnostics

## Scope

This revision strengthens the real-corpus validation pipeline for the candidate-survival diagnostics introduced in v0.91 and wired into the pinned UD Japanese-GSD/Sudachi evaluation in v0.92.

## CI enforcement

After the sequential decoder evaluates the held-out UD Japanese-GSD test split, the real-data workflow parses its JSON report and requires all three fields:
- `candidate_survival_diagnostic`
- `candidate_survival_by_domain`
- `candidate_survival_by_reading_length`

For the overall report and every individual group, CI enforces:
- `classified_gold_tokens == gold_tokens`
- `unclassified_gold_tokens == 0`
- the mutually exclusive outcome counts sum exactly to `gold_tokens`

For each stratification, the sum of grouped gold-token counts must equal the overall gold-token count. Empty or absent group maps fail closed.

## Interpretation

The grouped test-split output is diagnostic only. It must not be used to select thresholds or tune model parameters. Policy selection remains restricted to the dev split. A successful CI run validates the pipeline and accounting invariants, not an accuracy improvement.

## Resource limits

The real Japanese Pack workflow still permits up to 1,792 MiB RSS. The 100 MiB target remains separately enforced only for the bounded decode microbenchmark. No full-Pack 100 MiB claim is made.
