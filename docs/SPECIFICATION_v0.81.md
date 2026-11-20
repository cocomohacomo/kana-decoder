# kana-decoder Specification v0.81

## 32-bit compact Trie references

The compact Trie now uses 32-bit unsigned integers for:
- child-node indices in edge references;
- edge-arena offsets and counts per node;
- candidate-arena offsets and counts per node.

This reduces each edge reference from a typical 16-byte pair to 8 bytes on 64-bit platforms, and reduces the four per-node arena range fields from 32 bytes to 16 bytes. The two per-node vector objects remain unchanged; eliminating those requires a separate builder/storage redesign.

## Safety bounds

- Node creation rejects insertion before a child index would exceed the 32-bit representation.
- Edge and candidate arena compaction checks aggregate sizes before narrowing offsets/counts.
- Per-node candidate insertion is bounded to the representable 32-bit count.
- Existing compacted/uncompacted traversal, sorting, and lookup behavior is preserved.
- The string pool retains its existing 32-bit offset guard.

## Expected impact and caveats

This is a structural memory optimization, not a measured RSS claim. The exact benefit depends on edge count, node count, and allocator overhead. The full Japanese pack must be benchmarked before claiming a real-world reduction. The 32-bit ceiling is an explicit pack-format/runtime constraint: no arena or node index may exceed the representable range.

## Verification

CI results are pending for this commit. The 100 MiB guard remains a synthetic decode microbenchmark, not a claim that the full Japanese pack fits within 100 MiB.
