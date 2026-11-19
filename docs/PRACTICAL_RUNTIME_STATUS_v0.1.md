# kana-decoder practical runtime status

Updated: 2026-11-19
Branch: `refactor/surge-core-as-runtime`
Current contract: `SPECIFICATION_v0.51.md`

## Product acceptance target

The product is a kana-to-Japanese-text decoder. It accepts pure kana and emits a contextually selected Japanese surface containing kanji, hiragana, katakana, Latin text, digits, symbols and punctuation. SURGE remains the sole context and final-decision engine. The host must not introduce a second persistent decision system.

## Implemented in this stage

- Added provider-neutral `LanguageDecodeOptions` for path count, beam width and bounded input/lattice/search budgets.
- Changed the Japanese Language Pack provider to translate the neutral execution policy into its private Japanese decoder options.
- Removed the former `DecoderOptions` overload from the Language Pack contract; the C API and conformance tests now use `LanguageDecodeOptions`.
- Expanded Language Pack conformance coverage for path limits, input limits, invalid UTF-8, legacy forwarding, deterministic decoding and streaming transactional behavior.
- Added specification v0.49.
- Added explicit `japanese_decoder.hpp` and `japanese_language.hpp`; removed Japanese decoder policy, lexicon, lattice, scorer, morphology, correction and diagnostic declarations from generic `engine.hpp`.
- Made `LanguageAdapter` provider-neutral by using generic `Candidate` plus explicit score; Japanese adapter declarations now live in the Japanese Language Set header.
- Added `LanguageAdapter::render_path` and Japanese path renderer with validated spans, opt-in number rendering, optional Latin/Japanese spacing and discourse punctuation.
- Routed the SURGE UnifiedDecision-selected path through the configured LanguageAdapter; added adapter and runtime integration tests.
- Moved the Japanese LanguageAdapter implementation/numeric renderer and Japanese SURGE Cluster sidecar scorer into `language_sets/ja-JP/`; both are now compiled by the shared PIC-enabled Language Set object target.

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

The v0.51 source and test changes are committed to the development branch. Latest Actions run #969 is queued; CI for this head, full golden-corpus parity, held-out quality, target-PC latency and Peak RSS have not yet been verified. Therefore this status does not mark practical operation as complete.

The separate SURGE repository and its development branches are not modified by this work.
