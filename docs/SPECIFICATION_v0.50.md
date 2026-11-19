# kana-decoder: Japanese SURGE adapter ownership v0.50

Version: 0.50.0
Parent: SPECIFICATION_v0.49.md

## 1. Objective

Ensure Japanese language policy and Japanese evidence resources are built as part of the replaceable `ja-JP` Language Set rather than the generic host library.

## 2. Source ownership migration

The following sources moved from `src/` into `language_sets/ja-JP/`:

- `surge_language.cpp`: Japanese LanguageAdapter implementation, Japanese number parsing and typed numeric entity realization, plus the associated fixed-capacity language-state operations.
- `surge_cluster.cpp`: Japanese SURGE Cluster sidecar loading and candidate-context evidence scoring.

The CMake object target `kana_decoder_ja_language_set` now compiles both sources alongside the Japanese lattice, morphology, lexicon, scorer, correction, Pack loader and provider. Static and shared libraries consume the same PIC-enabled Language Set object target. The generic host source lists no longer compile these two Japanese sources.

## 3. Runtime contract

- The generic SURGE Core remains the only context and final-decision runtime.
- Japanese number parsing/rendering and Japanese cluster-sidecar scoring remain language-specific adapter/resource behavior.
- The C API and Language Pack contract remain provider-mediated.
- The separate SURGE repository and its vendored Core snapshot are not changed.

## 4. Verification

The source relocation and CMake ownership changes are committed. Static/shared build, CTest, Python integration, golden-corpus parity, held-out CER, edge-device Peak RSS and p95 latency remain pending CI/benchmark confirmation. This is an ownership/build-boundary change and does not claim a recognition-accuracy improvement.

## 5. Remaining work

- Split the generic public headers from Japanese lattice/trie/morphology/scorer and adapter implementation declarations.
- Move the remaining language-specific policy and complete surface renderer behind the Language Pack boundary.
- Replace the `ja-lite` test fixture with a licensed, production-coverage Japanese dictionary Pack.
- Measure candidate recall, CER, sentence exact match, streaming commit accuracy and target-device resources.
