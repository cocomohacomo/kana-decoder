# kana-decoder: Intern repeated candidate metadata strings v0.77

Version: 0.77.0
Parent: SPECIFICATION_v0.76.md

## 1. Goal

Reduce resident dictionary bytes by deduplicating repeated POS and inflection metadata in the candidate string pool without changing candidate output or public decoding behavior.

## 2. Representation

- Candidate IDs and rendered surfaces remain separately stored because they are commonly unique and must preserve candidate identity.
- POS, inflection type, and inflection form are stored through a metadata interning table.
- Empty metadata uses an empty reference and consumes no string-pool bytes.
- Each distinct non-empty metadata value is appended to the existing byte pool once. Later candidates reuse its offset/length reference.
- The interning table owns one key per distinct metadata value and stores only a compact offset/length reference.
- References remain valid if the backing std::string reallocates because only offsets are retained.

## 3. Compatibility and tests

- Materialized LatticeCandidate values retain the original POS and inflection strings.
- Candidate lookup and lattice construction are unchanged.
- Regression coverage checks repeated POS/inflection values, exact pool byte accounting, and round-trip materialization.
- Read-only storage counters expose pool bytes and the number of interned metadata values for deterministic tests and diagnostics.

## 4. Trade-offs and limits

The interning hash table adds one key and hash-table overhead per unique metadata value. This is expected to be favorable for the small, repeated POS/inflection vocabulary, but actual full Japanese Pack RSS must decide whether the trade-off pays off. IDs and surfaces are not interned. No full-pack memory reduction or 100 MiB achievement is claimed.
