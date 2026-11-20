# kana-decoder: Compact candidate arena and POS index v0.76

Version: 0.76.0
Parent: SPECIFICATION_v0.75.md

## 1. Goal

Reduce duplicated resident bytes in the candidate POS index while retaining the public lookup API.

## 2. POS index representation

- The index continues to own candidate IDs as lookup keys.
- POS values are now compact references (offset and length) into the lexicon's existing candidate string pool.
- Lookup materializes a returned std::string from the pool, preserving the public API.
- If one candidate ID is associated with conflicting POS values, the index stores an empty reference, preserving the previous ambiguity behavior.
- Rebuilding the index supports both mutable per-node candidate vectors and the compact candidate arena.

## 3. Compatibility and correctness

- Public candidate_pos(candidate_id) signature is unchanged.
- The string pool is append-only and references remain stable across std::string reallocation because offsets, not string_views or pointers, are retained.
- Mutation still requires rebuilding the POS index to reflect newly added candidates, matching the explicit build_candidate_pos_index lifecycle.
- Regression tests cover lookup before and after candidate arena expansion and index rebuild.

## 4. Memory claims and limitations

This removes one owned POS std::string value per indexed candidate ID, but index keys remain owned strings and unordered_map bucket/node overhead remains. No fixed RSS savings are claimed. Real Japanese Pack peak RSS and steady-state RSS remain measured by CI. The 100 MiB target remains unachieved.
