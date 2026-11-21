# kana-decoder Specification v1.06: Bounded SURGE retrieval adjacency

Parent: SPECIFICATION_v1.05.md

## Scope

This revision reduces repeated graph-relation scans in the kana-decoder SURGE adapter. It does not modify the vendored SURGE Core, graph semantics, candidate scoring, focus depth, frontier cap, claim cap, or relation-window selection.

## Retrieval algorithm

- The adapter first selects the same bounded relation window: up to 4,096 earliest relations and then up to 4,096 latest non-overlapping relations.
- When an active focus exists, it builds a temporary adjacency index from that bounded window in one pass.
- `Contains` relations add child-cluster edges and preserve claim-membership order.
- `AgreesWith` and `ConflictsWith` relations add bidirectional cluster-neighbor edges, as in the prior traversal.
- Breadth-first traversal remains depth-limited to two hops and at most 16 clusters.
- Claim roots remain collected in original relation-window order and capped at 64.
- Existing `retrieval_relations_scanned` and truncation reporting retain their meanings.

## Complexity

Previously, each visited cluster rescanned the full relation window. With a maximum of 16 visited clusters and an 8,192-relation window, this could perform roughly 131,072 relation inspections per normalization, before node lookups.

The revised flow inspects the relation window once to build the bounded adjacency view, then traverses only neighbors of visited clusters. Temporary memory is proportional to the bounded relation window. The graph remains authoritative; no persistent shadow graph or unbounded cache is introduced.

## Compatibility and validation

- Traversal depth, cluster frontier cap, claim cap, relation selection, and relation ordering are preserved.
- Existing runtime integration tests cover focus retrieval, cross-cluster evidence, dependency reassessment, and decision selection.
- The CI SURGE runtime benchmark must be compared using its repeated median/min/max output. A performance improvement is not claimed until a same-runner baseline comparison is available.
- The lightweight four-case reference Pack is a smoke test only, not a Japanese-wide accuracy evaluation.
