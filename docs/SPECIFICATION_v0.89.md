# kana-decoder Specification v0.89

## Candidate-loss accounting invariants

The candidate-survival diagnostic now exposes `classified_gold_tokens` and `unclassified_gold_tokens`. The loss categories are mutually exclusive and must partition every gold token:

- `lattice_missing`: no matching dictionary lattice candidate.
- `shortlist_drop`: present in the lattice but absent from the morphology Top-8 shortlist.
- `expansion_drop`: shortlisted but not observed in the bounded score trace, when trace capacity did not censor the case.
- `trace_censored`: absence from the score trace cannot be interpreted because the trace cap was reached.
- `beam_pruning_drop`: scored candidate whose expanded state did not survive its final destination beam.
- `final_selection_drop`: survived destination beam but did not align to the final best path.

The required invariant is:

`classified_gold_tokens == gold_tokens` and `unclassified_gold_tokens == 0`.

The `beam_pruning_drop` count is incremented only in the mutually exclusive classification branch. The independent `beam_survived` and `beam_pruning_drop` summary counts must also satisfy:

`scored_or_expanded == beam_survived + beam_pruning_drop`.

A deterministic narrow-beam unit test creates eight scored candidates with a beam width of two and verifies exactly two survivors and six drops. These checks validate diagnostic accounting, not language accuracy or a production beam-width recommendation.
