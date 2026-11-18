# kana-decoder 仕様書 v0.25

- Version: 0.25.0
- Core license: MIT
- Runtime: CPU-only / offline
- Development priority: correctness and functional completion before model-size optimization.

## 1. Candidate-generation diagnostics

- `LatticePositionTrace` records one entry per Unicode code-point start position.
- Each position reports:
  - dictionary match count and the matched spans;
  - candidate IDs and output surfaces retained on each emitted dictionary edge;
  - whether kana fallback was enabled;
  - whether fallback was generated;
  - whether fallback was suppressed because at least one dictionary match existed.
- `LexiconTrie::build_lattice` accepts an optional trace output pointer. Existing callers remain source-compatible through its default argument.
- `decode_reading` accepts an optional final lattice-trace output pointer. Existing call sites remain source-compatible.
- The trace describes the current generation policy; it does not change edge generation, sorting, candidate truncation, fallback decisions, or decoder scoring.

## 2. Evaluator interface

- `kana_decoder_eval PACK_DIR CORPUS_TSV --lattice-trace` emits case-level `candidate_generation` records.
- `--trace --lattice-trace` emits both score decomposition and candidate-generation traces.
- Trace output is diagnostic-only. Overall and per-domain aggregate metrics must remain identical to normal evaluation.
- Candidate-generation records are bounded by the reading length and the existing maximum dictionary matches per start. Candidate lists reflect the existing per-edge retained candidate set, not every raw dictionary row.

## 3. Regression requirements

- Unit tests verify a position with dictionary matches suppresses fallback and a position with no dictionary match generates fallback.
- CLI contract tests verify trace JSON shape, one record per reading code point, fallback state consistency, and unchanged aggregate metrics.
- The Sudachi Pack workflow runs the real Japanese Pack smoke corpus with both score and candidate-generation traces enabled.
- No candidate-generation policy is changed in this version. Any future policy change requires separate accuracy evaluation against a representative held-out corpus.

## 4. Compatibility and safety

- Existing public APIs and C ABI behavior remain unchanged; only optional C++ diagnostic parameters and data structures are added.
- Runtime operation without trace collection does not allocate trace records.
- Pack format, scoring formulas, candidate ordering, fallback policy, decoder bounds, and correction behavior remain unchanged.
- The v0.24 warning-as-error build policy remains mandatory.
- GitHub Actions run `44381437986` completed successfully: warning-free build, CTest 14/14, bounded decode benchmark, reference Pack verification, and Python suite 45 tests (41 passed, 4 skipped). The production Sudachi Pack trace workflow is tracked separately and must complete before claiming real-Pack diagnostic results.
