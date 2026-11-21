# kana-decoder Specification v1.11: Exact candidate-loss attribution

Parent: SPECIFICATION_v1.10.md

## 1. Scope

This revision corrects the candidate-survival diagnostic so that dictionary absence, span-edge selection, and per-terminal shortlist pruning are measured as separate stages. It does not change runtime candidate retention, scoring, beam width, or output ranking.

## 2. Defect found

The previous evaluator inferred `lattice_missing` by searching only the candidates already present in `LatticePositionTrace.dictionary_matches`. Those entries are emitted after the trie has applied its bounded per-terminal shortlist. Consequently, a gold surface that existed in the full dictionary but was discarded before trace emission was incorrectly counted as absent from the lattice. In addition, the diagnostic could not distinguish a dictionary match whose span edge was not retained by the per-position span cap.

This made `lattice_missing` and `shortlist_drop` unsuitable for choosing the next optimization.

## 3. Corrected read-only dictionary query

`LexiconTrie::contains_surface(reading, surface)` traverses the existing trie and checks the complete terminal candidate range, including compact and builder storage modes. It:
- performs no candidate materialization or heap allocation;
- does not mutate the trie or build a secondary index;
- compares UTF-8 reading and surface bytes exactly;
- inspects all terminal candidates before the top-eight shortlist;
- returns false for invalid UTF-8, empty values, absent reading paths, or absent surfaces.

The method is diagnostic and read-only. It does not affect normal decode behavior.

## 4. Mutually exclusive gold-token loss stages

Each gold token is classified in this order:

1. `lattice_missing`: no exact reading/surface pair exists anywhere in the dictionary.
2. `span_selection_drop`: the dictionary pair exists, but its exact start/end edge is not present in the retained lattice trace.
3. `shortlist_drop`: the exact edge exists, but its surface is absent from the retained top-eight candidate shortlist.
4. `trace_censored`: the shortlist contains the surface, but score tracing is capped before its scoring status can be observed.
5. `expansion_drop`: the shortlist contains the surface, but the candidate was not expanded/scored.
6. `beam_pruning_drop`: it was scored but did not survive beam pruning.
7. `final_selection_drop`: it survived the beam but was not selected by the final best path.
8. `final_selected`: it is aligned in the final best path.

The evaluator validates that these eight outcomes partition all eligible gold tokens. The same classification is aggregated by domain and reading-length bucket. The `dev-diagnostic` mode evaluates the frozen dev-calibrated policy on the development split without running another parameter search; its aggregate report is retained as a CI artifact. Held-out `test` remains a separate run. The existing RSS, lattice, candidate, trace, expansion and beam ceilings are unchanged.

## 5. Regression coverage

- Exact membership is tested for a surface retained in top-eight.
- Exact membership is tested for a surface present in the dictionary but outside top-eight.
- Missing surface and mismatched reading return false.
- Both mutable builder storage and compacted Pack storage are covered.
- Sequential evaluator tests require the new `span_selection_drop` field and include it in mutually exclusive outcome and rate partitions.
- CI validation requires eight loss-rate categories and rejects incomplete partitions.
- The pinned workflow runs the frozen dev policy in `dev-diagnostic` mode and enforces the same 1,792 MiB process ceiling; only aggregate diagnostics are retained.

## 6. Interpretation and next decision

The corrected diagnostic must be run on development data before changing candidate budgets, shortlist scoring, or span sampling. A high `shortlist_drop` points to per-terminal surface retention; a high `span_selection_drop` points to span-edge selection; a high `lattice_missing` points to dictionary coverage/import. These are different remedies and must not be conflated.

Held-out test remains diagnostic only and must not be used to tune candidate budgets or score weights. No accuracy improvement is claimed by this diagnostic correction.

## 7. Constraints

- No unbounded candidate retention or persistent cache.
- No relaxation of the 100 MiB decode microbenchmark RSS ceiling or 1,792 MiB hosted Pack evaluation ceiling.
- No raw corpus sentence traces committed or uploaded.
- Do not modify the separate SURGE repository or vendored `third_party/surge`.
- Keep PR #2 Draft and unmerged pending measured repeatable improvement and explicit authorization.
