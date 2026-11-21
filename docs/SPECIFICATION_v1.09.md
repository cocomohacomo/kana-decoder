# kana-decoder Specification v1.09: Non-owning SURGE surface evidence keys

Parent: SPECIFICATION_v1.08.md

## Scope

This revision removes avoidable surface-string copies from the SURGE cluster evidence aggregation performed by the morphology decoder. It does not change evidence scores, candidate ordering, cluster resolution thresholds, or decoding decisions.

## Implementation

For each active beam state, the SURGE cluster path aggregates evidence by distinct candidate surface. The candidate surfaces are owned by the immutable lattice edge and remain alive for the full aggregation and decode operation.

The aggregation now stores `std::string_view` in both the surface-score records and the lookup map instead of owning `std::string` keys. The views point directly into the edge candidate records. The map and score vector remain local to the beam-state evaluation and are destroyed before the lattice can be modified or released.

This avoids copying each distinct surface into the score vector and again into the hash map. It does not remove hash-table nodes or change the bounded candidate/edge limits.

## Compatibility and safety

- Candidate surface bytes and their lifetime are unchanged.
- Hash/equality semantics remain byte-exact and case-sensitive.
- Duplicate surfaces continue to update the maximum evidence score for the same surface.
- Best and second-best surface selection retains first-seen tie behavior.
- Cluster winner, gap threshold, override bonus, and downstream scoring remain unchanged.
- The views must not escape the local aggregation scope or outlive the lattice edge.

## Validation

The complete C++ suite, Python tests, real-Pack memory check, and pinned Sudachi Small workflow remain required. Performance and RSS improvements must be established from comparable runs; this revision makes no numerical performance claim by itself.
