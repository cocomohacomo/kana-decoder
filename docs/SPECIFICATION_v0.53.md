# kana-decoder: SURGE SemanticDelta output persistence compatibility v0.53

Version: 0.53.0
Parent: SPECIFICATION_v0.52.md

## 1. Problem

The vendored SURGE Core `SemanticDelta` contract persists graph nodes, relations, scope spans, focus and validity updates. It does not expose an arbitrary `journal_events` member. The previous kana-decoder integration attempted to append a normalization event through that unsupported member, preventing both static and shared targets from compiling.

## 2. Implementation

- Remove the unsupported `SemanticDelta::journal_events` access.
- Represent each normalization proposal/commit as a typed SURGE Evidence graph node.
- Store the event kind and complete normalization decision payload in the Evidence content.
- Add the output Evidence node to a deterministic `NORMALIZATION_OUTPUT` child Cluster using `ContextGraph::append_cluster_membership`.
- Persist the output record atomically with the candidate Claims and their evidence through the supported `ContextGraph::apply(SemanticDelta)` transaction.
- Keep the separate SURGE upstream repository and vendored SURGE Core source unchanged.

## 3. Invariants

- Output records are persisted in the same SemanticDelta transaction as the interpretation/evidence records.
- Proposal vs commit is explicit in the Evidence content.
- Stable event IDs derive from the decode prefix and finalization/ambiguity state.
- The event is reachable from the context graph via typed cluster membership.
- No second journal writer or host-owned decision store is introduced.

## 4. Verification

A prior CI build failed in both static and shared targets at `delta.journal_events`, because the member does not exist in the vendored Core API. The source fix is committed; a fresh CI run on the final PR head is required to confirm compilation and tests. No runtime or accuracy improvement is claimed until CI and benchmark evidence are available.

## 5. Remaining release gates

- Confirm static/shared build and complete CTest/Python suites.
- Verify output proposal/commit recovery after process restart.
- Verify deterministic event identity and idempotent replay.
- Run held-out Japanese accuracy and target edge-PC resource measurements.
