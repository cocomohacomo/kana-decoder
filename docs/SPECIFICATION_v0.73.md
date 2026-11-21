# kana-decoder: Packed Trie transition arena v0.73

Version: 0.73.0
Parent: SPECIFICATION_v0.72.md

## 1. Goal

Reduce per-node transition allocation overhead after dictionary ingestion while preserving the public Trie API and dictionary lookup behavior.

## 2. Runtime representation

- Trie construction and incremental insertion continue to use sorted per-node transition vectors.
- Once a TSV or gzip TSV dictionary has been parsed successfully, transition pairs are copied into one contiguous edge arena.
- Each node retains an edge offset and edge count; its temporary transition vector allocation is released.
- Lookup performs binary search over the node's contiguous arena range.
- Candidate payload vectors remain node-local in this version. Candidate string interning and packed candidate records are deferred.
- Arena offsets and counts use size_t, avoiding a new 32-bit dictionary-size ceiling.

## 3. Mutation compatibility

- Calling add() on a compacted Trie first reconstructs the mutable per-node transition vectors and releases the arena.
- Subsequent insertion and lookup therefore retain the prior incremental API behavior.
- A successful load atomically replaces the destination Trie only after parsing and compaction complete.

## 4. Memory interpretation

This change targets transition-vector allocation capacity and allocator overhead. It does not remove the per-node vector object, candidate vectors, candidate strings, or candidate POS index. The temporary edge arena may briefly coexist with node transition vectors during compaction, so peak load-time memory can exceed steady-state transition storage.

No real-pack RSS reduction, CER gain, or throughput gain is claimed until exact-head CI and production-pack benchmarks complete. The 100 MiB target remains unachieved.

## 5. Compatibility and validation

- Raw TSV and gzip TSV loading remain supported.
- UTF-8 code-point traversal, sorted transition order, candidate retention, trace accounting, and resource ceilings remain unchanged.
- Tests cover lookup after gzip loading and add-after-compaction expansion.
- SURGE main repository and third_party/surge remain unchanged.
- Draft PR #2 remains unmerged pending explicit authorization.
