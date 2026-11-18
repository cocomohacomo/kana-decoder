# kana-decoder 仕様書 v0.22

- Version: 0.22.0
- Core license: MIT
- Runtime: CPU-only / offline
- Development priority: correctness, diagnostic transparency and functional completion before size optimization.

## 1. Decoder module boundary

- The N-best morphology decoder implementation is extracted from `src/engine.cpp` into `src/morphology_decoder.cpp`.
- Public model and API declarations remain in `include/kana_decoder/engine.hpp`; existing decode calls remain source-compatible through a default-null optional trace argument.
- The extraction preserves beam width, edge/candidate/path ceilings, candidate scoring and path ordering. It is a translation-unit separation, not a scoring-policy change.

## 2. Candidate score trace

- `CandidateScoreTrace` reports reading code-point span, candidate ID/surface, word cost, candidate-local connection cost, transition connection cost, raw context score, context weight, morphology score, weighted context score, local score and unknown-fallback flag.
- Score identities:
  - `morphology_score = -(word_cost + candidate_connection_cost + transition_cost)`
  - `weighted_context_score = raw_context_score * context_weight`
  - `local_score = morphology_score + weighted_context_score`
- `decode_reading` and `MorphologyDecoder::decode` accept an optional trace vector. Existing callers that omit it retain the original behavior.
- Trace collection is capped at 4,096 expanded candidate transitions per decode to keep diagnostic memory bounded. The cap does not alter decoding.
- Unknown fallback is identified by the reserved candidate ID prefix `kana:`. The current lattice builder creates fallback edges only where no dictionary match begins; a dictionary match can therefore suppress fallback at that position. This behavior is surfaced for diagnosis, not silently changed.

## 3. Evaluator diagnostics

- `kana_decoder_eval PACK_DIR CORPUS_TSV --trace` enables per-case details and candidate-level trace JSON.
- Each trace record contains the score components above, allowing dictionary candidate selection and fallback availability to be inspected without changing runtime ranking.
- `--trace` and `--details` can be supplied together; unknown options are rejected.
- The evaluator's aggregate metrics are unchanged by trace mode.

## 4. Regression tests

- `decoder_trace_tests` verifies candidate decomposition, morphology and context score identities, and transition cost propagation.
- `evaluator_cli_contract_tests` verifies that `--trace` returns valid JSON with per-case candidate trace while preserving aggregate metrics.
- Trace tests do not claim an improvement in recognition accuracy. The four-case production smoke corpus remains diagnostic only.
- Verified in GitHub Actions run `44377822634` (code commit `713eb22ec4712a43cccac47909b2ab1106d6c042`): Configure / Build / CTest / benchmark / Pack verification / Python Pack and license tests all passed; CTest 14/14 PASS, including `decoder_trace_tests` and `evaluator_cli_contract_tests`.

## 5. Remaining work

- Inspect real Sudachi candidate traces for あめ / きしゃ / ぴ to distinguish dictionary candidate costs, matrix transitions, context-model absence and fallback suppression.
- Decide whether fallback candidates should coexist with dictionary matches only after inspecting trace evidence and evaluating false-positive impact.
- Train n-gram data from a source-separated corpus and validate on held-out documents.
- Continue separating Pack loading, trie/lattice construction and correction pipeline from the facade without changing public semantics.
