# kana-decoder Specification v0.96: Paired SURGE diagnosis

## Scope

This revision extends v0.95 with explicit paired comparisons between the morphology-only baseline and the selected SURGE evidence-gated decoder. The comparison is computed for the same held-out sentences and the same domain and reading-length strata.

## New test-report fields

- `surge_vs_morphology_by_domain`
- `surge_vs_morphology_by_reading_length`

Each group reports:

- `baseline_cer`, `surge_cer`, and `cer_delta`
- `baseline_exact_accuracy`, `surge_exact_accuracy`, and `exact_accuracy_delta`
- `baseline_token_alignment_recall`, `surge_token_alignment_recall`, and `token_alignment_recall_delta`
- Baseline and SURGE edit distances, plus the shared reference code-point count

Delta direction is always SURGE minus morphology. Therefore, negative CER delta is favorable; positive exact-accuracy or token-alignment-recall delta is favorable. These are descriptive diagnostics, not an automatic promotion criterion.

## Integrity requirements

Comparison groups must have the same keys as their paired baseline and SURGE metric maps. Each group's reference length must agree between both decoders. Tests recompute every delta from the underlying grouped metrics to detect stale, mismatched, or incorrectly signed reports.

## Evaluation policy

The test split remains report-only. Do not tune decoder weights, candidate budgets, or thresholds from these comparisons. Any policy changes must be selected on the development split and then evaluated once on held-out test data.

## Runtime and memory

The comparison is arithmetic over already aggregated group metrics. It performs no additional decoding, stores no per-sentence predictions, and changes no production inference path. The 100 MiB bound remains limited to the bounded decode microbenchmark; it is not a full-Pack memory guarantee.
