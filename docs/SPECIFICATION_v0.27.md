# kana-decoder 仕様書 v0.27

- Version: 0.27.0
- Core license: MIT
- Runtime: CPU-only / offline
- Priority: contextual correctness and evidence provenance before size optimization.

## 1. Scoring decomposition

The candidate score is now exposed as three independent parts:
- Unigram prior: frequency prior for a candidate.
- Context evidence: left/right masked n-gram evidence from neighboring morphology.
- Morphology/local score: dictionary word cost, candidate connection cost, transition connection cost, and weighted total scorer score.

`MaskedNgramScorer::contextual_score` returns only left/right context contribution. `score` remains the sum of the unigram prior and contextual contribution, preserving the existing scoring formula and ranking behavior. The decoder trace records both `raw_context_score` (legacy total scorer output) and `context_evidence_score` (neighbor-context-only component).

## 2. Evidence-gated SURGE probe

- Candidate surfaces at the same reading span form a claim cluster.
- Two or more competing surfaces form an unresolved Question.
- The probe ranks contextual evidence separately from morphology/local costs. For each surface it keeps the context evidence attached to the same best-local-path observation, avoiding maxima taken from incompatible beam histories.
- A Question is eligible for resolution only when at least two competing surfaces have differentiated, non-zero neighbor-context evidence and the best-vs-second context margin meets the configured threshold (default 0.5).
- Morphology cost, word frequency prior, gold labels, and the baseline output cannot resolve a Question.
- Unresolved Questions receive Focus, prioritized by smallest available context margin. If evidence is absent, the Question remains unresolved.
- Gold labels are used only for candidate coverage measurement. The probe never mutates decoder output.
- This is a lightweight diagnostic SURGE-inspired layer, not a full semantic SURGE implementation.

CLI:
```sh
python tools/evaluate_surge.py evaluation.json --context-margin 0.5
```

## 3. Decoder hard ceilings

- Direct calls to `MorphologyDecoder::decode` also clamp expansion states to 262,144; callers cannot bypass the ceiling by setting a larger option.
- A zero expansion budget fails closed and sets `resource_limit_exceeded`.
- Existing `score` values and candidate ranking remain unchanged by the decomposition.

## 4. Validation

- Unit tests verify unigram/context decomposition, differentiated context evidence, unresolved no-context questions, context-supported question resolution, and direct decoder fail-closed behavior.
- Real Sudachi Pack run `44385008956` (v0.26 baseline) completed successfully:
  - Peak RSS 1,425,500 KiB, below the 1,792 MiB gate.
  - Four smoke cases: 1/4 exact (25%), CER 1.0, decode failures 0.
  - Candidate coverage 4/4; 13 span clusters, 84 claims, 12 contradiction questions, 4 focused cases.
  - No learned n-gram evidence was present in the Pack; therefore no evidence-supported resolutions were observed.
- v0.27 changes require a fresh CI and real-Pack run. No accuracy improvement is claimed until a held-out corpus demonstrates it.

## 5. Compatibility

- Existing scorer `score` semantics and candidate ranking remain unchanged.
- C ABI, Pack file format, and existing decoder call sites remain compatible.
- Trace JSON gains `context_evidence_score`.