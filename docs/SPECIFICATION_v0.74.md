# kana-decoder: Compact candidate storage v0.74

Version: 0.74.0
Parent: SPECIFICATION_v0.73.md

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
