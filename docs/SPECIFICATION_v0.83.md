# kana-decoder Specification v0.82

## Frozen runtime Trie nodes

Mutable construction uses separate `BuildNode` instances that own edge and candidate vectors. The loaded runtime representation uses compact `Node` records containing only four 32-bit arena range fields. Child references and candidates reside in shared contiguous arenas.

After both arenas are frozen, the builder-node vector is released. Runtime lattice traversal and POS-index construction use frozen nodes and arenas. For a Trie built incrementally through `add()`, traversal continues to use the mutable builder representation until freezing.

## Mutation compatibility

Calling `add()` after loading expands frozen arenas into builder nodes before mutation. This preserves the mutable API, with a deliberate temporary memory cost during thawing. A subsequent freeze/load restores compact runtime storage.

## Safety and verification

32-bit node and arena bounds remain enforced. CI covers loading, traversal, duplicate handling, POS lookup, mutation after load, and repeated freeze/thaw cycles. The 100 MiB synthetic guard is not a full Japanese-pack memory claim.

## Storage capacity diagnostics

The Trie exposes read-only `storage_stats()` to report capacity bytes for frozen nodes,
edge/candidate arenas, mutable builder nodes and their nested vector capacities, plus the
candidate string pool. `estimated_bytes` sums those explicitly counted capacities.
It intentionally excludes allocator metadata, unordered-map buckets/keys, and unrelated
runtime objects; it is a structural estimate, not a substitute for process peak RSS.
Tests assert that loaded frozen Tries release builder capacities and that mutation after
load recreates builder storage.

## Progressive arena transfer

Freezing transfers each builder node's edge and candidate buffers into the contiguous runtime arenas using move iterators, then releases that node's source buffer immediately. This avoids retaining already-transferred per-node vector capacities until the entire arena has been built. Arena capacity is precomputed and reserved before transfer; 32-bit range checks run before mutation.

## v0.83 Candidate diversity stress validation

A deterministic stress regression executes 100 generated candidate sets with varied
insertion order and lexical costs. Each test set contains 24 candidates spanning
12 distinct surface groups while the retained cap is 8. The invariant is that
the capped output contains eight distinct surfaces; duplicate-surface candidates
must not crowd out unique alternatives. This is a retention regression test,
not a quality benchmark and does not imply a CER or sentence-exact improvement.

## Remaining evaluation gates

- Run exact-head CI and verify the new 100-case regression on the candidate branch.
- Measure the full Japanese dictionary pack peak RSS in the same CI environment;
  structural storage capacity diagnostics are not process-memory measurements.
- Re-run the pinned UD Japanese-GSD evaluation and compare Top-1, candidate
  recall, oracle CER, and SURGE policy results against the v0.65 baseline.
- Decoder and contextual scoring changes require held-out evidence before promotion.

## Evaluation diagnostics by reading length

The corpus evaluator reports aggregate exact-match accuracy, CER, and decode
failures in deterministic Unicode-code-point reading-length buckets:
1-4, 5-8, 9-16, and 17+ code points. These slices expose length-dependent
candidate loss and search degradation that can be hidden by corpus-wide means.
The buckets are diagnostic only; they do not alter decoding policy or scoring.


## v0.84 Rendered-hypothesis beam diversity

Beam pruning first retains the highest-scoring state for each distinct rendered
surface string, then fills any remaining beam slots in score order. This prevents
multiple token segmentations that render the same output from consuming the
entire beam and eliminating a lower-scoring distinct kanji/kana hypothesis.
The rule is deterministic and remains bounded by the configured hard beam cap.
It changes only intermediate hypothesis retention; it does not guarantee a
CER improvement. Held-out corpus evaluation is required before changing the
default beam width or scoring weights.
