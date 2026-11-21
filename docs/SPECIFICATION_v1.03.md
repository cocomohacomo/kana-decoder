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

All eligible lattice candidates are still scored and participate in the same surface-level aggregation and bounded top-evidence shortlist. The decoder retains only two bounded score stores: scores for up to four morphology seeds, and scores for the top evidence candidates (at most the hard candidate limit plus the seed count). After expansion, selected candidates reuse the retained score from one of these stores. No candidate-sized score map is retained, and no evidence score is recomputed for the final shortlist.

The seed store is necessary because a morphology seed may rank below the evidence shortlist while still being emitted. The evidence shortlist has capacity for the hard candidate limit plus the seed count, which is sufficient to supply every selected non-seed candidate even when all seeds also appear in the shortlist. This preserves candidate evaluation, surface aggregation, evidence ranking, tie order, and emitted candidates while removing duplicate score lookups. The selected-score vector remains bounded by the hard candidate limit. Runtime and peak-RSS effects remain subject to real-Pack CI measurements.
