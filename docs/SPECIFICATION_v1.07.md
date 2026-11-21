# kana-decoder Specification v1.07: Ordered trie candidate retention

Parent: SPECIFICATION_v1.06.md

## Scope

This revision aligns the small-terminal LexiconTrie candidate shortlist with the morphology decoder's bounded ordered-insertion strategy. It changes shortlist maintenance only; dictionary contents, candidate costs, surface-diversity semantics, lattice limits, and decoding scores remain unchanged.

## Retention contract

- Keep at most eight candidates per terminal node in the lattice shortlist.
- Candidates with the same rendered surface share one retained representative.
- A strictly cheaper candidate replaces the existing representative for that surface.
- Equal-cost duplicates preserve the first-seen representative.
- Retained candidates remain ordered by ascending word-plus-connection cost.
- Equal-cost distinct surfaces preserve their prior encounter order.
- If the shortlist is not full, the existing fallback pass fills remaining slots in source order while maintaining cost order.

## Implementation

The small-terminal path (up to 16 dictionary candidates) now inserts retained candidate pointers at their ordered position using `std::upper_bound`. It no longer re-sorts the entire shortlist after every insertion or cheaper duplicate replacement.

The shortlist is bounded at eight entries. The insertion position is found over this bounded list; vector insertion may shift at most seven pointers. Candidate surfaces and records are not copied during shortlist maintenance.

The larger-terminal compact-record path is unchanged in this revision.

## Regression requirements

The C++ suite verifies that:
- a cheaper duplicate surface replaces its more expensive representative;
- equal-cost distinct surfaces retain first-seen order;
- the output shortlist is cost ordered;
- the shortlist remains capped at eight;
- the selected candidate IDs match the expected prior retention behavior.

## Validation and performance claims

This is a bounded-work and implementation-consistency refactor. No wall-clock speedup or accuracy gain is claimed without same-runner benchmark evidence. The full C++/Pack CI and pinned Sudachi Small real-Pack workflow remain required. Real Pack peak RSS and held-out accuracy must be reported separately from the four-case lightweight smoke Pack.
