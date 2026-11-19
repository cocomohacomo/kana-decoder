# kana-decoder: provider-neutral Candidate morphology and replayable normalization commits v0.56

Version: 0.56.0
Parent: SPECIFICATION_v0.55.md

## 1. Unified normalization journal

- Normalization proposal/commit records and explicit streaming prefix/final commits are persisted as typed Evidence nodes through SURGE `ContextGraph::apply(SemanticDelta)`.
- The runtime no longer writes opaque normalization stream events directly to the shared EventJournal. This keeps every normalization state transition inside the graph's replayable transaction protocol.
- Surface provenance is represented by Evidence nodes connected to the interpretation Claim through `Supports` relations. Entity nodes remain reference-free, matching the SURGE Core node contract.
- Dependency nodes are appended after their prerequisite Claim and Entity nodes have been staged, so references are resolvable during atomic delta application.

## 2. Provider-neutral Candidate morphology

The generic `Candidate` no longer exposes Japanese-specific `pos`, `inflection_type`, or `inflection_form` members. It carries a `MorphologyDescriptor` with:

- `language_tag`: provider/language identifier;
- `part_of_speech`: provider-defined grammatical category;
- `inflection_class`: provider-defined inflection class;
- `inflection_form`: provider-defined inflection form.

The generic host treats these fields as opaque metadata. The Japanese Language Set maps its private lattice metadata into a `ja-JP` descriptor. The Japanese Language Adapter rejects a non-empty foreign language tag instead of interpreting foreign morphology as Japanese.

The Japanese-specific `LatticeCandidate` remains private to `japanese_decoder.hpp` and the ja-JP Language Set.

## 3. Test and CI reliability

- Language Pack identity parsing now matches the complete JSON key, including its closing quote.
- The renderer/context trace test verifies that at least one beam state with observed left-context evidence receives a non-zero contextual contribution, without assuming unsupported beam states also have evidence.
- SURGE recovery tests exercise normalized output, temporal reassessment and stream commit replay through the shared ContextGraph journal.
- The public-header boundary script is import-safe under Python unittest discovery and remains separately invoked by CTest.
- A foreign-language morphology rejection case is included for the Japanese adapter.

## 4. Verification status

- CI run #1011 passed: static/shared build, all 29 CTest cases, bounded decode and SURGE microbenchmarks, Japanese reference Pack verification, policy calibrator syntax check, and Python Pack/license policy tests.
- CI run #1012 is required to verify the v0.56 Candidate metadata refactor and its new conformance checks.
- CI success does not establish held-out CER, Candidate Recall@N, streaming commit accuracy, target-PC latency, or Peak RSS. These remain separate release gates.

## 5. Compatibility

- The C API contract is unchanged.
- The generic C++ `Candidate` layout changed. No stable external C++ ABI is promised at this development stage; consumers of the in-progress C++ header API must rebuild.
- No files in the upstream SURGE repository are modified. The vendored SURGE Core remains a snapshot and is consumed through its existing APIs.
