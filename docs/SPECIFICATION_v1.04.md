# kana-decoder Specification v1.04: Exact bounded beam identity

Parent: SPECIFICATION_v1.03.md

## Scope

This revision records the morphology decoder's beam-pruning identity refactor and its regression contract. It does not change candidate scoring, transition scoring, beam width, candidate limits, path limits, or the SURGE evidence model.

## Beam identity

- Intermediate beam states are equivalent only when their concatenated rendered surface, normalized last-right identifier, word history, and POS history are equal.
- Final-position diversity compares concatenated rendered surfaces only.
- Rendered-surface equality is evaluated byte-for-byte over the same ordered UTF-8 token surfaces as the former concatenation-based key. Token segmentation boundaries do not affect equality.
- A missing last transition normalizes to identifier zero, matching the former serialized identity.
- The beam width remains hard-capped at eight. Exact comparisons replace per-state serialized identity allocation and hash-set insertion. No hash-only identity is permitted.

## Bounded work and behavior

The uniqueness scan compares each candidate state with at most the already-retained beam states. Since the hard beam width is eight, the number of identity comparisons per prune is bounded by 8 x 8. Surface comparison may inspect the concatenated token bytes; it does not allocate a concatenated identity string. Existing fallback behavior remains: if fewer than the beam limit distinct identities exist, remaining states fill the unused slots in their prior stable score order.

## Regression requirements

The C++ regression suite covers:
- Same rendered prefix with distinct word histories and continuation IDs remains independently eligible at intermediate positions.
- Distinct token segmentations with the same concatenated surface are considered equal for final diversity.
- Final diversity prefers a distinct rendered surface over a duplicate when the beam is full.
- A cheaper candidate replaces a more expensive candidate with the same surface in the bounded morphology shortlist.
- Equal-cost duplicate surfaces preserve first-seen order.
- Candidate shortlist size remains within the configured limit.

## Validation and performance claims

The C++ build uses warnings-as-errors. The complete C++/Pack workflow and pinned Sudachi Small real-Pack workflow are required. Compare benchmark results under identical runner/configuration before attributing latency or memory changes to this refactor. No performance improvement or accuracy improvement is claimed by this specification alone. Hosted CI results do not establish target business-PC latency.

The pinned real-corpus workflow continues to use train-only sequences, dev-only policy calibration, one frozen held-out test evaluation, and the 1,792 MiB peak-RSS gate defined by the real-corpus evaluation specification.
