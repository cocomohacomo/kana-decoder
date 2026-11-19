# kana-decoder: Japanese decoder API separation and surface renderer v0.51

Version: 0.51.0
Parent: SPECIFICATION_v0.50.md

## 1. Objectives

- Remove Japanese decoder implementation types from the generic public `engine.hpp`.
- Keep the SURGE LanguageAdapter interface language-neutral.
- Provide a Language Set-owned Japanese surface renderer for safe orthographic realization.
- Route the final selected path through the configured LanguageAdapter after SURGE UnifiedDecision.

## 2. Public header boundaries

### Generic host API: `kana_decoder/engine.hpp`

Retains only generic candidate/path data, UTF-8 validation, generic dependency graph, streaming snapshot/session state and ambiguity rendering.

### Japanese Language Set API

- `kana_decoder/japanese_decoder.hpp`: Japanese N-gram scorer, correction policy, lattice and connection types, Japanese decoder options, lexicon trie, morphology decoder, Japanese resource loader and diagnostics.
- `kana_decoder/japanese_language.hpp`: Japanese LanguageAdapter implementation declaration, Japanese number parsing and rendering helpers.

C API and evaluation/conformance targets that intentionally use Japanese internals include the explicit Japanese header. Generic LanguagePack and SURGE runtime headers do not include the Japanese decoder API.

The generic `LanguageAdapter::make_hypothesis` accepts a provider-neutral `Candidate` plus an explicit score. Japanese lexical-cost fields no longer cross the adapter interface.

## 3. Surface rendering contract

`LanguageAdapter::render_path` accepts a selected `DecodedPath`, original reading and provider-neutral `SurfaceRenderOptions`. It returns a rendered sentence without mutating the input path.

The Japanese implementation:
- validates UTF-8 and every token source span against the original reading;
- can normalize Japanese numeric readings to Arabic digits when explicitly enabled;
- can insert spaces at ASCII-word/Japanese-script boundaries when explicitly enabled;
- can append discourse-appropriate Japanese or ASCII terminal punctuation when explicitly enabled;
- rejects malformed or overlapping spans and preserves the caller's output buffer on failure.

Numeric normalization defaults off in the path renderer because the numeric alternative is already submitted to SURGE UnifiedDecision as a competing surface. This avoids an unreviewed renderer override of the Core's selected surface.

## 4. SURGE integration

After UnifiedDecision selects the interpretation path and each token's surface hypothesis, the runtime constructs the selected path and passes it through the configured LanguageAdapter. The renderer is therefore downstream of the authoritative decision and is restricted to explicit, policy-controlled surface operations.

## 5. Conformance

Added tests cover:
- generic engine header no longer declaring Japanese decoder types;
- Japanese adapter candidate scoring through provider-neutral Candidate;
- numeric reading rendering;
- Japanese statement/question punctuation;
- optional numeric rendering;
- invalid source-span rejection with transactional output preservation;
- SURGE-selected numeric surface followed by Japanese sentence rendering.

## 6. Verification status

Source and tests are committed to the development branch. Latest GitHub Actions run #969 is queued. No build/test pass, CER, throughput, latency or RSS improvement is claimed until CI and benchmark evidence are available.

## 7. Remaining practical-release requirements

- Complete orthography and inflection/okurigana realization using dictionary-backed rules rather than heuristic edits.
- Add date/time/unit/currency/range/identifier extraction and render alternatives with source evidence.
- Expand sentence-level punctuation and spacing policy with discourse-aware tests.
- Supply a production-coverage Japanese dictionary and connection model.
- Run held-out CER, sentence exact match, Candidate Recall@N, streaming commit quality, p95 latency and Peak RSS on the target edge PC.
- Verify clean offline installation and stable external ABI.
