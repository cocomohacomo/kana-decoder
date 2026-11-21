# kana-decoder Specification v0.97: Candidate bottleneck attribution

## Scope

This revision extends v0.96 candidate-survival reporting with a deterministic summary of the largest mutually exclusive gold-token loss stage. It applies to the overall report and each domain and reading-length stratum.

## New fields

- `dominant_loss_stage`: one of `lattice_missing`, `shortlist_drop`, `expansion_drop`, `trace_censored`, `beam_pruning_drop`, `final_selection_drop`, or `none`
- `dominant_loss_tokens`: gold-token count attributed to that stage
- `dominant_loss_pct`: percentage of all gold tokens attributed to that stage
- `loss_stage_rates_pct`: percentages for all six loss stages and `final_selected`

The dominant stage is selected by the largest count. Ties are resolved by the fixed order listed above. If every loss count is zero, the stage is `none` and the count and rate are zero.

## Integrity requirements

The loss-stage rates plus the final-selected rate must sum to 100 percent when gold tokens exist, and to zero for an empty group. The dominant stage and count must agree with the underlying mutually exclusive counters. Existing partition invariants remain mandatory: each gold token is classified exactly once, with zero unclassified tokens.

## Interpretation

- `lattice_missing`: dictionary lattice did not contain the gold span and surface
- `shortlist_drop`: the gold candidate was present in the lattice but outside the morphology shortlist
- `expansion_drop`: shortlisted gold candidate did not reach scoring/expansion
- `trace_censored`: scoring trace cap prevents attribution between expansion and later stages
- `beam_pruning_drop`: gold candidate was scored but did not survive beam pruning
- `final_selection_drop`: candidate survived beam pruning but was not selected as the final aligned output
- `final_selected`: gold candidate was selected

This summary is diagnostic, not an automatic tuning signal. Policy selection remains development-split-only; held-out test results remain report-only.

## Runtime and memory

The summary is computed from existing counters after decoding. It adds no decode pass, retains no per-sentence output, and does not alter production inference. The 100 MiB bound remains limited to the bounded decode microbenchmark, not the full Japanese Pack.
