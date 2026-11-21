# kana-decoder Specification v1.01: Cluster-path lookup optimization

## Scope

This revision records a semantics-preserving performance refactor in the Japanese morphology decoder. It does not change candidate ranking, cluster scoring, beam limits, context weights, or output policy.

## Cluster evidence aggregation

The decoder previously aggregated candidate evidence by scanning the accumulated surface-score vector for every candidate. It also searched the complete supported-candidate vector again for each retained candidate when emitting score traces and decoded-token evidence flags. For a high-cardinality lattice edge, these repeated linear scans could make cluster evaluation quadratic in the number of candidates.

The implementation now:
- maintains a surface-to-score-index hash table while aggregating evidence, preserving first-seen surface order and maximum evidence score per surface
- maintains a candidate-pointer-to-evidence hash table for constant-average-time evidence retrieval during candidate expansion
- retains the existing stable descending sort for surface and candidate evidence ranking
- reserves bounded hash-table capacity based on the edge candidate count, capped initially at 256 buckets to avoid speculative large allocations

The supported-candidate vector and its stable score ordering remain unchanged for candidate expansion. No candidate is added or removed by this optimization solely because of the lookup representation.

## Validation

The cluster decoder regression fixture now includes 2,050 candidates on the ambiguous edge and still asserts that cluster evidence rescues the intended candidate outside the morphology shortlist. CI must pass the C++ cluster tests and the full existing test suite before this optimization is considered validated.

This is an algorithmic complexity improvement for cluster-enabled high-cardinality edges, not a measured end-to-end speedup claim. Real Pack latency and peak RSS must be read from the completed Japanese Pack workflow. The 100 MiB gate applies only to the bounded decode microbenchmark unless a separate full-Pack measurement explicitly reports that limit.

## Non-goals

No production weights or candidate limits are changed. No training or held-out test data is used for parameter selection. SURGE upstream and vendored source files are not modified.
