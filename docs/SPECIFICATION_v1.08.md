# kana-decoder Specification v1.08: Large-terminal shortlist parity

Parent: SPECIFICATION_v1.07.md

## Scope

This revision aligns the compact-record shortlist path for trie terminals with more than 16 candidates with the ordered-retention contract used by terminals containing at most 16 candidates.

## Retention contract

Both paths use the same bounded selection semantics:

- Retain at most eight candidates.
- During the primary pass, retain one representative per surface where possible.
- Replace a retained surface representative only when a strictly cheaper candidate appears.
- When a cheaper replacement is inserted, order it by ascending total candidate cost using upper-bound insertion. Equal-cost entries already retained remain before the replacement.
- A new distinct surface enters a full shortlist only when its cost is strictly below the current worst retained cost.
- If fewer than eight candidates survive the primary pass, the existing source-order fallback fills remaining slots, including repeated surfaces, while preserving ascending cost order.
- Equal-cost ordering is stable relative to the order in which retained entries are inserted.

## Implementation

The >16 compact-record path no longer maintains an `unordered_map<string_view, index>` for at most eight selected entries. It uses a bounded linear surface lookup and ordered insertion into the shortlist. This removes hash-table node allocation and prevents stale slot indices when a cheaper duplicate is repositioned.

The shortlist remains capped at eight. The selected records remain references into the immutable candidate arena during lattice construction; only retained candidates are materialized.

The <=16 path and >16 path now share the same replacement and tie-order behavior. Candidate storage, terminal threshold, lattice limits, fallback policy, and decoder scoring are unchanged.

## Regression requirements

The C++ suite verifies:
- a >16-candidate terminal retains the expected cheapest surface-diverse shortlist;
- cheaper duplicate replacement preserves the earlier equal-cost candidate ahead of the replacement;
- repeated-surface fallback behavior remains bounded;
- candidate order is ascending by total cost;
- shortlist size remains at most eight.

## Complexity and validation

For each candidate in a large terminal, surface lookup scans at most eight retained entries. Ordered insertion shifts at most eight small selection records. Thus shortlist maintenance is bounded by O(N × 8), with no per-terminal hash-table node allocation.

This is a determinism and bounded-allocation refactor. No throughput or peak-RSS improvement is claimed until measured on the same runner and corpus. The full C++/Pack CI and pinned Sudachi Small real-Pack workflow remain required. The real Pack held-out test must remain untouched by dev calibration.
