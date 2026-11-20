# kana-decoder: Compact candidate arena v0.75

Version: 0.75.0
Parent: SPECIFICATION_v0.74.md

## 1. Goal

Reduce per-candidate resident memory without changing the public `LatticeCandidate` API or emitted lattice values.

## 2. Internal representation

- Trie terminal nodes store fixed-layout `CompactCandidate` records instead of owning five independent `std::string` objects.
- Candidate strings (ID, surface, POS, inflection type, inflection form) are appended to one contiguous byte pool.
- Compact string references contain checked 32-bit byte offsets and lengths.
- Numeric cost and connection fields remain signed 32-bit values.
- Public candidates are materialized only when selected lattice results are emitted.
- Candidate POS indexing reads compact references and retains the existing public lookup behavior.

The pool is append-only in this version. It centralizes storage and removes per-string object/allocation overhead; it does not yet deduplicate identical byte sequences. That optimization is deliberately deferred until measurements justify its indexing overhead.

## 3. Compatibility and safety

- Public `LatticeCandidate` and `LatticeEdge` layouts remain unchanged.
- Raw TSV and gzip TSV loaders both use the compact candidate path.
- UTF-8 validation applies to every candidate string field.
- Pool offset/length overflow is checked before append.
- Add-after-edge-compaction remains supported.
- Selected output candidates are reconstructed with all original string and numeric fields.

## 4. Memory and performance claims

The internal record replaces five string objects with five offset/length pairs and four numeric fields. This is expected to reduce per-candidate object overhead, especially for short strings, but actual RSS benefit depends on string lengths, vector capacity, and allocator behavior.

No measured RSS reduction, CER improvement, or throughput improvement is claimed until the exact-head workflow and real Japanese pack benchmark finish. The 100 MiB target remains unachieved.

## 5. Deferred work

- String-content deduplication with a measured break-even policy.
- Packed candidate arena per terminal node.
- Lazy or compact candidate POS index.
- Peak-memory reduction during initial dictionary construction.


## v0.75: terminal candidate arena

- Loaded lexicons flatten all per-node compact candidate vectors into one contiguous `CompactCandidate` arena. Each trie node stores a candidate offset and count, eliminating one vector object and its allocation capacity per terminal node after loading.
- Lattice construction reads candidates directly from the compact arena and materializes only the public `LatticeCandidate` objects needed by the output lattice.
- `add()` expands the candidate arena back into mutable per-node vectors before inserting, matching the existing edge-arena mutation contract. The loader compacts both arenas only after successful parsing.
- POS-index construction reads either mutable node vectors or the compact arena, so its public behavior is unchanged.
- Added regression coverage for load, lookup, post-load mutation, preserved entries, and POS lookup after arena expansion.
- This is a structural allocation reduction. It does not claim a specific RSS, load-time, throughput, or CER improvement; the next CI run must measure the real Japanese Pack peak RSS. Compaction temporarily holds source vectors and destination arena simultaneously, so load-time peak memory may be higher than steady-state memory.
