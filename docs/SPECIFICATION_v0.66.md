# kana-decoder: Candidate recall and bounded pruning v0.66

Version: 0.66.0
Parent: SPECIFICATION_v0.65.md

## 1. Problem statement

Candidate diagnostics from v0.65 showed that many gold token spans were not represented in the decoder's top-10 paths. Both the lattice builder and morphology decoder applied cost-only caps. Prefix matches were truncated in increasing end-offset order, potentially discarding longer dictionary matches before search.

## 2. Surface-diverse candidate retention

- Under the per-edge candidate cap, candidates are ranked by lexical plus connection cost.
- Retention first selects the cheapest candidate for each distinct rendered surface, then fills unused slots with remaining candidates in cost order.
- The retained list is restored to cost order for deterministic beam expansion.
- The same policy applies in the lexicon lattice builder and morphology decoder, including caller-supplied lattices.
- Existing per-edge hard cap remains eight.

## 3. Span-diverse edge retention

- The trie scans all matching prefixes within the bounded input length, then samples up to the per-start edge cap across the complete end-offset range.
- The first and longest matching spans are retained when the match count exceeds the cap.
- The morphology decoder applies the same deterministic span sampling when caller-supplied lattices exceed the hard cap.
- The per-start edge cap remains sixteen. Global edge, candidate, and expanded-state ceilings are unchanged.

## 4. Diagnostics

The real-data evaluator reports available, retained, and pruned dictionary matches and candidates. The calibrator validates both partitions fail-closed and stores the pruning object in each dev-policy row. These diagnostics do not affect policy selection.

## 5. Regression tests

- Duplicate-surface candidates with cheaper costs must not crowd all retained candidate slots.
- Twenty nested prefix matches under a sixteen-edge cap must retain the longest span and report four pruned matches.
- Pruned match and candidate counts must partition their available totals.

## 6. Safety and limitations

- Candidate retention is deterministic and bounded; it does not guarantee higher CER.
- Distinct rendered surfaces are a recall-oriented diversity heuristic. Morphological and contextual evidence still determine final ranking.
- No test-split metric is used for policy tuning.
- SURGE main repository and vendored SURGE Core are unchanged.
