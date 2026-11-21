# kana-decoder: Evaluation-driven candidate and decision hardening v0.68

Version: 0.68.0
Parent: SPECIFICATION_v0.67.md

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
