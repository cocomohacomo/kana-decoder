# kana-decoder: Japanese Language Set conformance v0.47

Version: 0.47.0
Parent: SPECIFICATION_v0.46.md

## 1. Purpose

Fix the Japanese Language Set's lattice-generation and bounded-search behavior with deterministic synthetic fixtures. This release does not tune ranking weights or alter decoding policy.

## 2. Conformance rules

1. For candidates attached to one reading span, lattice candidate order is ascending by word cost plus candidate connection cost. Equal-cost behavior remains stable relative to insertion order.
2. If at least one dictionary match begins at a reading position, the one-codepoint kana fallback is suppressed at that same position.
3. If no dictionary match begins at a position and fallback is enabled, one explicit fallback candidate is generated for that code point with ID prefix `kana:`.
4. Exceeding the configured lattice candidate ceiling sets the resource-limit flag.
5. Exceeding the configured lattice edge ceiling sets the resource-limit flag.
6. Exceeding the morphology expansion budget rejects the decode and does not return a partial best path.
7. A resource-limit condition must be observable by the caller and must not be mistaken for a successful complete decode.

## 3. Executable coverage

`tests/test_japanese_language_set.cpp` builds a synthetic trie independent of the shipped dictionary and checks candidate ordering, fallback suppression, unknown-kana fallback, candidate ceiling, edge ceiling and expansion-budget exhaustion.

The test is registered as `japanese_language_set_conformance_tests`.

## 4. Architecture status

Japanese recognition implementation files are compiled in the dedicated `kana_decoder_ja_language_set` object target. However, `include/kana_decoder/engine.hpp` still exposes Japanese implementation structures and classes, and generic host code still includes that header. The next architecture task is to move those implementation declarations behind a Language Set provider boundary without changing the public C/Python API or the SURGE UnifiedDecision contract.

## 5. Verification

The new CTest target and all existing static/shared, C API, Python, smoke-quality and SURGE integration tests must pass in CI. Golden-corpus output parity and target-PC CPU/RSS/latency measurements are separate required gates.

No build/test success, CER improvement, latency improvement, RSS reduction or package-size improvement is claimed until measured results are available.

## 6. Scope

Only `cocomohacomo/kana-decoder`, branch `refactor/surge-core-as-runtime`, is changed. The separate SURGE repository is untouched.
