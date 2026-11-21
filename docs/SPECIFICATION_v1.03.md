# kana-decoder Specification v1.03: Bounded SURGE evidence shortlist

## Scope

This revision records a semantics-preserving optimization in the Japanese morphology decoder's SURGE cluster-evidence path. It does not change the cluster model, evidence scores, gap threshold, candidate limit, ranking weights, or final output policy.

## Cluster surface aggregation

The decoder still evaluates cluster evidence for every eligible lattice candidate and aggregates the maximum evidence score for each distinct surface. Instead of sorting every distinct surface, it now scans the completed aggregates once to find the best and second-best surfaces. Strict comparisons preserve first-seen order for equal scores, matching stable descending sorting.

## Supported candidate expansion

The previous implementation retained and stable-sorted every candidate with cluster evidence, although only a small bounded number could be appended to the morphology shortlist. The decoder now maintains a stable descending shortlist bounded by:

- the configured hard candidate limit (at most eight)
- plus the number of morphology seeds (at most four)

This is sufficient even if every seeded morphology candidate is also present in the evidence shortlist. Equal evidence scores retain original lattice order. The shortlist is then merged with the same morphology seeds, with duplicate candidate pointers skipped as before.

The full candidate-to-evidence lookup remains available for per-token trace fields. Candidate evaluation itself is not truncated.

## Validation

The existing SURGE cluster regression includes a candidate rescued from outside the morphology top four and a high-cardinality lattice edge. The complete C++ and Pack test workflow is required after this change. No numeric latency or memory improvement is claimed until the corresponding benchmark completes.

## Retained SURGE evidence memory

All eligible lattice candidates are still scored and participate in the same surface-level aggregation and bounded top-evidence shortlist. The decoder no longer retains a candidate-pointer-to-score hash table for every supported candidate on the edge. After morphology seeds and the bounded evidence shortlist are merged, it recomputes evidence only for the final expanded candidates (at most the hard candidate limit) and stores those values in a compact vector for trace emission.

This trades at most a small bounded number of repeated evidence lookups per beam state for removal of an edge-cardinality-sized score map. It does not truncate candidate evaluation, change evidence ranking, alter shortlist ordering, or change the emitted candidate set. A regression assertion verifies that a rescued candidate still carries its SURGE evidence in the score trace. Runtime and peak-RSS effects remain subject to real-Pack CI measurements.
