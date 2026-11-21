# kana-decoder Specification v0.94: Stratified sequential quality metrics

## Scope

This revision extends the real-corpus sequential evaluator with quality metrics grouped by input domain and reading length. The change is diagnostic only and does not alter decoding, candidate generation, ranking, or SURGE policy.

## New report fields

The held-out test report adds:

- `surge_metrics_by_domain`
- `surge_metrics_by_reading_length`

Each group reports the same end-to-end metrics as `surge_evidence_gated_left_context`: case counts, exact matches and accuracy, edit distance, reference code points, CER, decode failures, over-limit sentences, gold/predicted token counts, token alignment precision, and token alignment recall.

Reading-length groups use Unicode code-point length and the existing buckets:

- `1-4`
- `5-8`
- `9-16`
- `17-32`
- `33-64`
- `65-128`
- `129-256`

Empty readings and readings longer than 256 code points are excluded from reading-length groups. They remain represented in overall metrics and domain groups, with over-limit counts retained. Group metrics are aggregated per complete sentence, not by pooling sentence-level percentages.

## Interpretation and policy

These metrics localize end-to-end quality differences that candidate-survival accounting alone cannot explain. They help distinguish domains or reading lengths where ranking, segmentation, or output quality degrades.

The test split remains report-only. Do not tune thresholds, weights, or decoder parameters against these groups. Policy selection remains dev-only. This change makes no claim of accuracy improvement.

## Resource and behavior guarantees

- No runtime decoder behavior changes.
- No new model data or persistent runtime state.
- Group aggregation reuses per-sentence decode results and does not trigger additional decoding passes.
- The 100 MiB target remains separately enforced only for the bounded decode microbenchmark; the full Japanese Pack evaluation retains its existing 1,792 MiB RSS ceiling.
