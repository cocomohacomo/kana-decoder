# kana-decoder Specification v1.00: Paired outcomes and controlled development ablations

## Scope

This revision builds on v0.99 paired sentence outcomes. It adds fixed one-factor-at-a-time component ablations to the sequential corpus evaluator's `dev-calibrate` mode. It does not alter production decoding, candidate generation, runtime policy, or the selected calibration grid.

## Development-only component ablations

The evaluator reports:
- `dev_ablation_left_context_only`: trained lexical n-gram scorer with the left-context evidence gate and fixed context weight 1.0; POS scorer and cluster model disabled
- `dev_ablation_pos_context_only`: trained POS n-gram scorer with fixed POS weight 0.15; lexical left-context weight and cluster model disabled
- `dev_ablation_selected_policy`: the policy selected by the existing development calibration, including its selected margin and optional cluster contribution
- `dev_ablation_cluster_only`: emitted only when a cluster sidecar is supplied; cluster model at fixed weight 1.0 with n-gram/POS context weights disabled
- `dev_ablation_scope`: the explicit value `development_only_diagnostic_not_selection`

Morphology-only and trained-unigram metrics remain separately available as reference controls.

These ablations are descriptive and are not included in the grid-search objective. The fixed component weights are experimental probes, not recommended production defaults. They run only in `dev-calibrate`, whose corpus split is hard-bound to `dev`. Test mode does not emit or execute these ablations. CI validates the split marker, required metrics, case-count consistency, and the conditional cluster-only result.

## Interpretation

Compare each component-only result with morphology-only and trained-unigram controls before attributing an improvement to a specific evidence family. A component can improve CER while reducing exact-match rate, or vice versa; both metrics must be considered. The selected-policy result is reported separately because it may combine components and is not a clean isolated effect.

These are controlled diagnostic comparisons, not proof of causal independence: component interactions, training data, and the decoder's candidate lattice remain shared. Any follow-up change to production weights or candidate-generation limits requires a separate controlled experiment and a held-out test report after selection is frozen.

## Resource behavior

Ablation metrics are accumulated online. No per-sentence predictions are retained. The extra decode passes occur only in the offline development evaluator and do not affect production inference or its streaming memory profile.

## Validation

C++ evaluator tests assert all ablation outputs are present in dev mode, have the same eligible case count as morphology-only, and contain bounded CER/exact metrics. The workflow rejects missing markers, non-dev split output, missing ablations, or inconsistent case counts. The existing test-split evaluator and paired-outcome report remain report-only.
