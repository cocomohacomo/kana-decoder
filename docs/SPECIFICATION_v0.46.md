# kana-decoder: Language Pack conformance hardening v0.46

Version: 0.46.0
Parent: SPECIFICATION_v0.45.md

## 1. Objective

Strengthen the Japanese Language Pack boundary with executable conformance checks. This stage is test-focused: it must not change decoding algorithms, candidate scoring, ranking, rendering, or resource policy.

## 2. Required invariants

1. A loaded Pack is exposed as an immutable shared snapshot.
2. Identical Pack and input must produce deterministic best-path surface sequences.
3. Input exceeding the configured code-point ceiling must be rejected, not silently truncated.
4. Invalid UTF-8 input must be rejected.
5. Incremental append across chunk boundaries must preserve the complete reading and converge to the same best-path surfaces as one-shot decoding for the tested fixture.
6. Separate streaming sessions sharing one Pack must not share mutable reading state.
7. Language Adapter identity must match the Pack language tag.
8. Numeric entity realization remains delegated to the Pack's Language Adapter.

## 3. Implemented checks

tests/test_language_pack.cpp now covers snapshot identity, deterministic repeated decode, input-budget rejection, invalid UTF-8 rejection, two-chunk streaming parity, session isolation and Japanese numeric adapter behavior.

These are targeted conformance checks, not a complete proof of all Language Set behavior. Dedicated tests for trie match ordering, unknown-kana fallback policy, lattice edge/candidate caps, expansion-budget exhaustion, and resource loading failure atomicity remain open.

## 4. Build and verification

The tests are part of immutable_language_pack_runtime_tests. The branch must pass strict-warning C++ compilation, static and shared library builds, all CTest targets, Python integration tests, golden-corpus parity and resource-budget checks.

Until the corresponding Actions run completes successfully, this specification records implementation intent and test coverage only. It does not claim a successful build, test run, accuracy improvement, latency improvement or memory improvement.

## 5. Scope

Changes are confined to cocomohacomo/kana-decoder, branch refactor/surge-core-as-runtime. The separate SURGE repository and its main branch are not modified.
