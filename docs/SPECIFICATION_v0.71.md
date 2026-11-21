# kana-decoder: Evaluation-driven candidate and decision hardening v0.70

Version: 0.70.0
Parent: SPECIFICATION_v0.69.md

## 1. v0.65 held-out evaluation

The completed baseline evaluation used 2,414 UD Japanese-GSD test segments and 10,317 gold tokens. The evaluation policy was selected using the development split only (`margin_guard_025`).

| Metric | Decoder Top-1 | SURGE Core |
|---|---:|---:|
| CER | 0.486337 | 0.495666 |
| Sentence exact | 0.246479 | 0.242336 |
| Runtime failures | 0 | 0 |
| Decode failures | 0 | 0 |

The SURGE Core policy did not improve this held-out test result. It was worse by 0.009329 absolute CER and 0.004143 sentence-exact points. The development-selected policy is therefore an experimental policy, not a production-default promotion.

Runtime for SURGE Core was 120.91 seconds (19.97 segments/second) with peak RSS 1,426,536 KiB under the configured 1,792 MiB ceiling.

## 2. Candidate-generation diagnosis

Test candidate metrics:

- Mean paths: 7.750621
- Exact sentence surface recall@1/@5/@10: 0.246479 / 0.451947 / 0.509114
- Token-aligned recall@1/@5/@10: 0.562567 / 0.694776 / 0.728991
- Oracle CER@5/@10: 0.332130 / 0.299085

At top-10 token attribution, 7,521 tokens had exact span and surface candidates, 1,835 had a span match but surface mismatch, 58 had a surface match at the wrong span, and 903 had no matching surface candidate. The largest actionable gaps remain candidate coverage and surface realization, not runtime exceptions.

Candidate-generation diagnostics are test measurements only. They must not be used to tune policy weights or thresholds.

## 3. v0.66 direct-caller regression coverage

The v0.66 policy is applied both by the lexicon trie and to caller-supplied lattices in MorphologyDecoder. This release adds direct-caller regression tests for:

- 20 competing spans at one start position under the 16-edge cap, ensuring the longest span remains available.
- Duplicate rendered surfaces under the 8-candidate cap, ensuring eight distinct surfaces survive when eight exist.

These tests complement the trie-side tests and verify that externally constructed lattices receive the same bounded diversity behavior.

## 4. Next evaluation protocol

1. Run the complete CI suite on the exact PR head.
2. Run the real-data test evaluator on the v0.66 candidate branch using the same pinned corpus and dictionary source locks.
3. Compare candidate recall, oracle CER, decoder Top-1, SURGE Core CER, sentence exact, elapsed time, and peak RSS against the v0.65 baseline.
4. Keep dev-only policy calibration separate from test evaluation.
5. Do not promote a policy unless held-out evaluation demonstrates improvement and resource limits remain satisfied.

## 5. Promotion and safety

- Keep candidate-diversity changes experimental until v0.66 test evaluation completes.
- Do not change SURGE main repository or vendored SURGE Core.
- Do not merge Draft PRs without explicit authorization.
- Preserve the hard input, lattice, candidate, trace, and expanded-state ceilings.
- No CER improvement is claimed by this specification.


## 6. v0.68 compact trie transitions

The Japanese lexicon trie stores outgoing transitions as compact vectors of Unicode code point and child-node index pairs. This removes a per-node unordered-map allocation strategy and avoids allocating a UTF-8 string key for each transition. Transition lookup is currently linear in the node's outgoing degree; this is intentional for the typically small branching factor and must be validated against real pack load and decode benchmarks.

Correctness requirements:

- Preserve UTF-8 validation before decoding code points.
- Preserve exact Unicode code-point boundaries and lattice offsets.
- Preserve insertion order and duplicate-entry behavior.
- Preserve all candidate, edge, input, trace, and expansion ceilings.
- Do not claim a memory or throughput gain until exact-head CI and real Japanese-pack measurements confirm it.

The change is experimental on the Draft candidate branch. The SURGE main repository and vendored SURGE Core remain unchanged.


## 7. v0.69 sorted compact transitions

Outgoing trie transitions are maintained in ascending Unicode code-point order. Insertions use lower-bound positioning, and lookups use binary search. This preserves the compact vector representation while changing transition lookup from linear to logarithmic in node degree.

The regression test inserts branches in deliberately non-sorted order and verifies exact two-code-point readings remain discoverable. Full Unicode and Japanese pack behavior remains subject to the exact-head CI and corpus evaluation.

No memory or throughput improvement is claimed until measurements are available. The sorted vector adds insertion movement proportional to the node's degree; dictionary construction and runtime lookup must both be measured.


## 8. v0.70 allocation-reduced UTF-8 trie traversal

Trie insertion and lattice construction no longer materialize a temporary std::string for every UTF-8 code point. The implementation advances through the original UTF-8 byte sequence, decodes each code point directly at its byte offset, and retains a compact byte-offset table for code-point-indexed lattice spans. Fallback candidate strings are materialized only when an unknown-character edge is actually emitted.

Correctness requirements:

- Validate UTF-8 before any code-point traversal.
- Preserve Unicode code-point offsets in lattice edges, including four-byte UTF-8 characters.
- Reject malformed UTF-8 and preserve the existing unknown-character fallback behavior.
- Keep all resource ceilings and candidate-diversity policies unchanged.
- Treat reduced temporary allocation as an implementation-level objective only; do not claim lower peak RSS or higher throughput without exact-head measurements.

Regression coverage includes four-byte UTF-8 lookup and malformed UTF-8 rejection. This remains an experimental candidate-branch optimization; the held-out CER baseline is unchanged and no accuracy improvement is claimed.


## 9. v0.71 streaming byte-offset traversal

Lattice construction no longer allocates or retains a per-input UTF-8 byte-offset vector. It first counts validated code points, then advances byte cursors monotonically: one cursor for the current start position and one for trie traversal. Lattice edges continue to expose Unicode code-point offsets. The fallback surface is materialized only for an emitted unknown-character edge.

Correctness requirements:

- Preserve validation before traversal and retain the existing 256-code-point and 1,024-byte input ceilings.
- Preserve Unicode code-point spans for mixed-width UTF-8 input, including four-byte emoji followed by Japanese characters.
- Preserve dictionary matching, fallback behavior, candidate pruning, trace accounting, and fail-closed resource limits.
- Do not claim lower peak RSS or higher throughput until exact-head CI and real-pack evaluation confirm the effect.

Regression coverage includes mixed-width UTF-8 input with both dictionary and fallback edges. This remains an experimental implementation optimization; no CER improvement is claimed.
