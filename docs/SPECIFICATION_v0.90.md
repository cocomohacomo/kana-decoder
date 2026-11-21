# Specification v0.90: Candidate survival accounting success partition

## Purpose

Clarify candidate-survival diagnostics so every gold token belongs to exactly one terminal outcome. v0.89 established loss-category accounting; this revision closes the remaining gap for candidates that survive the beam and are selected in the final best path.

## Mutually exclusive terminal outcomes

Each in-scope gold token is assigned to exactly one category:

- `lattice_missing`: no matching dictionary lattice edge exists.
- `shortlist_drop`: a lattice edge exists, but the gold candidate is outside the morphology shortlist.
- `trace_censored`: the candidate was not observed in the score trace and the trace reached its configured cap.
- `expansion_drop`: the candidate was in the shortlist but was not scored, with no trace-cap explanation.
- `beam_pruning_drop`: the candidate was scored but did not survive the destination beam.
- `final_selection_drop`: the candidate survived the beam but is absent from the final best path.
- `final_selected`: the candidate survived the beam and is present in the final best path.

The accounting invariant is:

```
gold_tokens ==
    lattice_missing + shortlist_drop + trace_censored + expansion_drop
    + beam_pruning_drop + final_selection_drop + final_selected
```

The separate trace invariant remains:

```
scored_or_expanded == beam_survived + beam_pruning_drop
```

`final_selected` is a successful terminal outcome, not a loss. The diagnostic does not itself imply an accuracy improvement; it only makes the candidate path accounting exhaustive and mutually exclusive.

## Scope

As before, this is evaluator instrumentation only. Runtime decoding policy, scoring weights, beam width, language-pack contents, and SURGE integration behavior are unchanged. No claim is made about full-pack peak RSS or held-out corpus quality.
