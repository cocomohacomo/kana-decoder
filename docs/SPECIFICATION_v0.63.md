# kana-decoder: Token candidate miss attribution v0.63

Version: 0.63.0
Parent: SPECIFICATION_v0.62.md

## 1. Purpose

Explain why a gold token is not covered by the top-10 decoder paths. The metric complements token-aligned Recall@N and does not alter candidate generation or SURGE decisions.

## 2. Attribution classes

For each trusted gold token, inspect every decoder token in the first 10 paths and classify it into exactly one category, in priority order:

- `exact_span_and_surface`: at least one candidate has the exact gold reading span and exact gold surface.
- `span_match_surface_mismatch`: no exact match exists, but at least one candidate occupies the exact gold reading span with a different surface.
- `surface_match_span_mismatch`: neither exact nor same-span match exists, but the gold surface occurs at a different reading span.
- `no_surface_candidate`: the gold surface does not occur in any token in the top 10 paths.

The priority makes the classes mutually exclusive and exhaustive. When the decoder returns no paths, each gold token is attributed to `no_surface_candidate`.

## 3. Invariants

The four counters must sum to `gold_tokens`. The exact-span-and-surface count must agree with token-aligned Recall@10 multiplied by the gold-token denominator, subject only to JSON decimal serialization precision. The black-box CTest checks the partition and consistency.

## 4. Interpretation

- `span_match_surface_mismatch` points toward lexical candidate ambiguity, inflection/surface realization or dictionary metadata at a known boundary.
- `surface_match_span_mismatch` points toward segmentation/lattice boundary mismatch.
- `no_surface_candidate` points toward missing lexicon coverage, unknown-word handling or search pruning.
- `exact_span_and_surface` means candidate generation exposed the gold token; remaining errors are downstream ranking, SURGE selection, or normalization.

These are diagnostic signals, not causal proof. The evaluator has no gold POS tag in the current corpus schema, so this version does not claim POS-specific error attribution.

## 5. Calibration and validation

The calibrator propagates the full `token_aligned_candidate_generation` object into each dev policy row for diagnosis only. These counters are not used to choose policy weights. The smoke corpus validates all class keys, the exhaustive partition and exact-count consistency. The pinned real corpus and held-out split remain necessary for product-level conclusions.
