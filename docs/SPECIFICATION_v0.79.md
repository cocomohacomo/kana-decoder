# kana-decoder Specification v0.79

## POS index construction peak-memory reduction

The candidate POS lookup index is now built in one backing vector:
- collect compact ID/POS string references directly into `candidate_pos_index_`;
- sort the vector in place by candidate ID;
- fold duplicate IDs in place, retaining the existing ambiguity rule (conflicting POS becomes empty);
- resize to the folded entry count without creating a second full-size index buffer.

This removes the former temporary `entries` vector and its simultaneous lifetime with the final index, reducing peak index-build allocation pressure. The vector may retain capacity from the original entry count after folding; this change does not claim capacity reduction or measured RSS reduction.

## Compatibility and verification
Lookup remains sorted-vector lower_bound O(log U). POS strings remain views into the LexiconTrie-owned compact string pool and are materialized at API return. Duplicate/conflicting ID behavior remains unchanged. Build/test results are reported only after CI for this commit completes.
