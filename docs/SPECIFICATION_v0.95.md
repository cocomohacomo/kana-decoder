# kana-decoder Specification v0.95: Paired stratified evaluation

## Scope

This revision extends v0.94 by reporting stratified metrics for both the morphology-only baseline and the selected SURGE evidence-gated decoder. The purpose is to make regressions and gains attributable to SURGE visible within each corpus domain and reading-length bucket.

## Report fields

The held-out test JSON contains four grouped reports:

- `morphology_metrics_by_domain`
- `morphology_metrics_by_reading_length`
- `surge_metrics_by_domain`
- `surge_metrics_by_reading_length`

Each group has the same fields as the corresponding overall report: cases, exact matches, exact accuracy, edit distance, reference code points, CER, decode failures, over-limit sentences, gold/predicted token counts, token alignment precision, and token alignment recall.

The morphology and SURGE reports use identical held-out sentences and grouping keys. Group case counts, edit distances, and reference code-point counts must sum to their respective overall values. The morphology and SURGE group key sets must match.

## Interpretation

Compare morphology and SURGE within the same group. A lower CER or higher exact accuracy is a favorable direction, but token alignment metrics should also be inspected because exact sentence accuracy can hide segmentation changes.

The test split is report-only. Do not tune any threshold, weight, or model parameter from these grouped test results. Calibration remains restricted to the development split.

## Runtime and memory

The added grouping reuses existing per-sentence decode outputs for each baseline and SURGE evaluation. It does not introduce additional decoder invocations, persistent state, or runtime inference behavior. The 100 MiB bound remains a bounded microbenchmark guard, not a full-Pack memory claim.
