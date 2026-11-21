# kana-decoder Specification v1.02: Bounded lattice materialization and UTF-8 scan reduction

## Scope

This revision records two semantics-preserving runtime changes in the Japanese decode path. It does not change dictionary ranking policy, beam width, candidate limits, scoring weights, or output normalization.

## Lazy materialization of lexicon candidates

The lattice builder previously materialized every candidate at each matched trie terminal, including copying all candidate strings, before reducing the set to a maximum of eight surface-diverse candidates.

The builder now performs shortlist selection directly over compact dictionary records. It retains only pointers to the selected compact records and materializes LatticeCandidate strings only for the final shortlist. The selection preserves:
- the existing maximum-cost ordering and stable tie behavior
- replacement of an already-retained surface by a cheaper candidate
- the existing fallback that fills unused shortlist slots with additional candidate records when fewer than eight unique surfaces exist
- the existing hard candidate and edge limits

This reduces transient string copies and allocation volume for high-cardinality dictionary terminals. The final lattice candidate records and ordering are intended to remain equivalent to the prior selection routine.

## UTF-8 input scan

`decode_reading` already validates UTF-8 before decoding. It previously validated the string a second time while constructing a vector of one allocated substring per codepoint, solely to obtain the reading length. It now counts codepoints in a single allocation-free pass after validation and passes that count to the morphology decoder.

The input validation rules and maximum codepoint limit remain unchanged.

## Validation

Regression coverage includes:
- high-cardinality candidate terminals with surface diversity and late cheaper duplicate replacement
- repeated-surface terminals that exercise the shortlist fallback
- existing Japanese smoke-quality, UTF-8 validation, C API, streaming, and memory-budget tests

Performance claims require completed CI benchmarks and real Japanese Pack evaluation. No numeric end-to-end speedup or peak-RSS reduction is claimed by this specification alone.
