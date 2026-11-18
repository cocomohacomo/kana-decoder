# kana-decoder 仕様書 v0.24

- Version: 0.24.0
- Core license: MIT
- Runtime: CPU-only / offline
- Development priority: correctness and functional completion before model-size optimization.

## 1. Compiler warning policy

- Both `kana_decoder_core` and `kana_decoder_shared` are compiled with `-Wall -Wextra -Wpedantic -Werror`.
- Compiler warnings are build failures for both static and shared targets.
- The warning cleanup in this version is semantics-preserving: it separates misleading one-line control flow and explicitly initializes optional metadata fields.
- No scoring formula, candidate ordering, fallback policy, correction threshold, decoder bound, Pack format, or public API was changed.

## 2. Diagnostic trace invariants

- Candidate trace continues to expose reading span, candidate identity/surface, lexical and transition costs, context score and weight, morphology score, weighted context score, local score, and fallback classification.
- For every trace entry:
  - `morphology_score = -(word_cost + candidate_connection_cost + transition_cost)`
  - `weighted_context_score = raw_context_score * context_weight`
  - `local_score = morphology_score + weighted_context_score`
- Trace collection remains bounded to 4,096 candidate expansions per decode.
- `--trace` remains diagnostic-only. Corpus aggregate metrics must equal the normal evaluator output.

## 3. Compatibility and safety

- Existing C++ declarations and C ABI remain unchanged.
- Explicit initialization of candidate metadata uses empty strings for fields that are intentionally absent in fallback and C API convenience candidates.
- Pack loading remains transactional.
- The module split and the prior v0.23 guarantees remain in force.

## 4. Build and test gates

- GitHub Actions must compile both static and shared libraries with warnings treated as errors.
- CTest, bounded decode microbenchmark, Japanese reference Pack verification, and Python Pack/license tests remain required.
- GitHub Actions run `44380200959` for code commit `4f9aa8c2076626dad075555e161fcae76d26093d`: completed / success. Both static and shared targets built with `-Werror`; Build log contained zero warnings/errors. CTest 14/14 PASS; bounded decode benchmark PASS; Japanese reference Pack verification PASS; Python suite 45 tests, 41 passed and 4 skipped.
