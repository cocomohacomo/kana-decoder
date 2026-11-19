# kana-decoder practical runtime status

Updated: 2026-11-19
Branch: `refactor/surge-core-as-runtime`
Current contract: `SPECIFICATION_v0.55.md`

## Product acceptance target

The product is a kana-to-Japanese-text decoder. It accepts pure kana and emits a contextually selected Japanese surface containing kanji, hiragana, katakana, Latin text, digits, symbols and punctuation. SURGE remains the sole context and final-decision engine. The host must not introduce a second persistent decision system.

## Implemented in this stage

- Added provider-neutral `LanguageDecodeOptions` for path count, beam width and bounded input/lattice/search budgets.
- Changed the Japanese Language Pack provider to translate the neutral execution policy into its private Japanese decoder options.
- Removed the former `DecoderOptions` overload from the Language Pack contract; the C API and conformance tests now use `LanguageDecodeOptions`.
- Expanded Language Pack conformance coverage for path limits, input limits, invalid UTF-8, deterministic decoding and streaming transactional behavior.
- Added specification v0.49.
- Added explicit `japanese_decoder.hpp` and `japanese_language.hpp`; removed Japanese decoder policy, lexicon, lattice, scorer, morphology, correction and diagnostic declarations from generic `engine.hpp`.
- Made `LanguageAdapter` provider-neutral by using generic `Candidate` plus explicit score; Japanese adapter declarations now live in the Japanese Language Set header.
- Added `LanguageAdapter::render_path` and Japanese path renderer with validated spans, opt-in number rendering, optional Latin/Japanese spacing and discourse punctuation.
- Routed the SURGE UnifiedDecision-selected path through the configured LanguageAdapter; added adapter and runtime integration tests.
- Moved the Japanese LanguageAdapter implementation/numeric renderer and Japanese SURGE Cluster sidecar scorer into `language_sets/ja-JP/`; both are now compiled by the shared PIC-enabled Language Set object target.
- Corrected the Sudachi V1 importer contract: POS5/POS6 are retained as conjugation type/form, explicit conjugation aliases are supported, and `*` sentinels are normalized to empty metadata.
- Added dictionary-backed inflection/okurigana conformance cases for `回っ` and `食べて`; selected dictionary surfaces are preserved exactly by the renderer rather than regenerated heuristically.
- Added multi-token inflection-chain tests for `回って` (`回っ` + `て`) and `食べている` (`食べて` + `いる`), verifying complete contiguous reading coverage and exact surface preservation.
- Fixed the SURGE output persistence integration: normalization proposal/commit records are now typed Evidence nodes in the same supported `SemanticDelta` transaction and attached to a `NORMALIZATION_OUTPUT` cluster. Removed unsupported `SemanticDelta::journal_events` usage.

## Remaining work before practical release

### P1: complete provider boundary
- Confirm static and shared builds plus C/Python integration after the header split.
- Further reduce generic `Candidate` morphology metadata if another language provider requires a distinct representation.
- Verify external Language Set consumers can include only the generic headers.

### P2/P3: recognition quality
- Verify candidate recall independently from candidate ranking.
- Confirm word/POS/transition/cluster evidence provenance and avoid correlated double counting.
- Exercise long-distance context, topic switching, contradictions, question routing, temporal expiry and reassessment against sequential corpora.
- Tune policy on dev only; retain held-out test data.

### P4: complete Japanese surface realization
- Implement and test dictionary-backed orthography, okurigana and inflection rules; the current renderer is intentionally conservative.
- Extend date/time/unit/currency/range/identifier recognition and render alternatives with source evidence.
- Expand punctuation, spacing, symbols and mixed-script output policy.
- Tie each rendered surface to its source interpretation Claim and submit all alternatives to the same SURGE UnifiedDecision.
- Do not apply heuristic surface changes that can alter meaning without evidence.

### P5/P6: streaming and edge runtime
- Test arbitrary chunk boundaries, provisional revisions, common-prefix commits, final commits, reset and session isolation.
- Measure peak RSS, load time, throughput and p50/p95 chunk latency on the target business PC.
- Keep immutable dictionaries/models shared across sessions; bound journal, graph retrieval and all decode work.

### P7/P8: release qualification
- Report CER, sentence exact match, Candidate Recall@N and streaming commit accuracy on held-out Japanese data.
- Produce reproducible static/shared and Python package builds.
- Verify offline clean installation, pack digest/version checks and rollback behavior.

## Verification status

The v0.55 source and test changes are committed to the development branch. Actions run #998 failed during Build because `tests/test_engine.cpp` still called the removed Japanese-specific `StreamingSession::append`; the test now uses provider-decoded paths with `append_decoded`. The earlier #992 run reached CTest and exposed Pack-load failures plus one malformed Sudachi TSV test delimiter. Loader diagnostics, active Release assertions and the delimiter correction are now committed; a fresh final-head CI run is required. Golden-corpus parity, held-out quality, target-PC latency and Peak RSS remain unverified, so practical operation is not yet complete.

The separate SURGE repository and its development branches are not modified by this work.
