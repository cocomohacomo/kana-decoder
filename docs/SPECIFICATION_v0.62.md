# kana-decoder: Gold-token-aligned candidate recall v0.62

Version: 0.62.0
Parent: SPECIFICATION_v0.61.md

## 1. Purpose

Add a token-aligned Candidate Recall metric when the evaluation corpus contains trusted gold token boundaries. This complements, and does not replace, sentence-level exact raw-surface Recall@N.

## 2. Gold alignment source

The six-column evaluation TSV format groups rows by document ID and sentence ID. Each row contributes one gold token pair: reading and surface. The evaluator concatenates token readings/surfaces to form the sentence case while retaining each gold token pair for alignment.

Three-column sentence-level corpora do not provide gold token boundaries. For those cases the token-aligned metric reports zero eligible gold tokens and JSON `null` recall values. The evaluator must not infer token boundaries from its own decoder output.

## 3. Matching rule

For each gold token, compute its half-open Unicode code-point reading span by cumulative token-reading length. A raw decoder path covers that gold token only when one decoded token has all three properties:

1. exactly the same start and end code-point offsets;
2. exactly the same candidate surface;
3. belongs to one of the first N returned paths.

Report token-weighted Recall@1, @5 and @10 over all gold tokens from six-column cases. The metric is independent of final SURGE selection and rendering. It measures exact token-span candidate availability, not morphological similarity or partial overlap.

## 4. Calibration and verification

The dev-only calibrator copies the token-aligned metric object into each candidate row for diagnosis. It does not use token recall to choose evidence-family weights. The black-box CTest invokes both the existing sentence-level smoke corpus and a six-column token-aligned fixture, checks the gold token denominator, and verifies Recall@1 <= Recall@5 <= Recall@10.

## 5. Interpretation and limitations

- Low token-aligned recall with higher sentence-level recall can indicate segmentation mismatch: the concatenated surface exists, but the decoder does not expose the gold token boundaries.
- Low sentence-level and token-aligned recall suggests candidate generation or dictionary coverage limitations.
- High token-aligned recall with poor final output points downstream toward ranking, SURGE evidence, or rendering.
- Token-level scores are valid only for corpus rows with trusted gold tokenization and reading spans. They are not reported for sentence-only data.
- No held-out quality gain is claimed until the pinned Sudachi Small + UD Japanese-GSD workflow completes.

## 6. Verification status

The implementation and smoke contract are committed. Final-head CI and the pinned real-corpus run remain required before the metric is considered production-validated.
