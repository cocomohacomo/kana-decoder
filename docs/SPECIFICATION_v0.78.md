# kana-decoder: Offset-based sorted POS index v0.78

Version: 0.78.0
Parent: SPECIFICATION_v0.77.md

## 1. Goal

Remove duplicate ownership of candidate ID strings and reduce per-entry hash-table overhead in the POS lookup index.

## 2. Representation

- The POS index is a sorted contiguous vector of pairs of compact ID and POS references.
- Both references point into the lexicon's existing append-only candidate string pool.
- Index construction gathers only candidates with non-empty ID and POS, sorts by ID, and folds each ID group into one entry.
- If a candidate ID has conflicting non-empty POS values, the folded entry has an empty POS reference, preserving the prior ambiguity behavior.
- Lookup uses lower_bound and compares string views against the caller's ID. No candidate ID key is copied into the index.
- The public candidate_pos API remains unchanged.

## 3. Complexity and lifecycle

- Build: O(N log N) comparison work and O(N) temporary compact references.
- Lookup: O(log U) comparisons, where U is the number of distinct indexed IDs.
- Persistent index storage: two compact references per distinct ID.
- Rebuild after dictionary mutation remains explicit, as in the previous API lifecycle.
- The sorted vector is contiguous and avoids unordered_map bucket and per-node allocation overhead.

## 4. Validation

Tests cover sorted lookup, unknown IDs, multiple candidate surfaces sharing an ID, and conflicting POS values. Storage diagnostics expose the number of distinct POS index entries.

## 5. Limitations

Index construction uses a temporary vector proportional to the number of candidates, so peak build memory may rise during index creation. This change targets steady-state memory; full Japanese Pack peak and steady RSS must be measured before claiming a net improvement. No 100 MiB target achievement is claimed.
