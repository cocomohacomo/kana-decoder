# kana-decoder Specification v0.87

## Beam pruning continuation-context diversity

The morphology decoder's diversity pass now deduplicates retained beam states by a bounded continuation identity, not by rendered surface alone. The identity combines:

- rendered surface so far;
- last right-connection ID;
- the decoder's capped token-ID history (up to three items);
- the capped POS history (up to three items).

At intermediate positions where continuation is needed, this avoids treating two paths as interchangeable when they render identically but carry different transition or contextual scoring state. At the terminal position, deduplication intentionally falls back to rendered surface so duplicate internal histories cannot occupy multiple final-output slots and crowd out distinct hypotheses. States remain score-sorted, the diversity pass is bounded by the existing beam width (hard maximum 8), and remaining slots are filled by score. This is a pruning heuristic, not an accuracy claim. Full-pack and UD Japanese-GSD dev/test comparisons are required before recommending a production policy.

## Validation boundary

The existing direct-lattice regression continues to validate that distinct rendered hypotheses survive segmentation crowding. CI must compile and run the full test suite after this change. The context identity is bounded by the existing history caps and beam limit; it does not add unbounded state.
