# kana-decoder practical runtime status

Updated: 2026-11-19
Branch: `refactor/surge-core-as-runtime`
Current contract: `SPECIFICATION_v0.50.md`

## Product acceptance target

The product is a kana-to-Japanese-text decoder. It accepts pure kana and emits a contextually selected Japanese surface containing kanji, hiragana, katakana, Latin text, digits, symbols and punctuation. SURGE remains the sole context and final-decision engine. The host must not introduce a second persistent decision system.

## Implemented in this stage

- Added provider-neutral `LanguageDecodeOptions` for path count, beam width and bounded input/lattice/search budgets.
- Changed the Japanese Language Pack provider to translate the neutral execution policy into its private Japanese decoder options.
- Removed the former `DecoderOptions` overload from the Language Pack contract; the C API and conformance tests now use `LanguageDecodeOptions`.
- Expanded Language Pack conformance coverage for path limits, input limits, invalid UTF-8, legacy forwarding, deterministic decoding and streaming transactional behavior.
- Added specification v0.49.
- Moved the Japanese LanguageAdapter implementation/numeric renderer and Japanese SURGE Cluster sidecar scorer into `language_sets/ja-JP/`; both are now compiled by the shared PIC-enabled Language Set object target.

## Remaining work before practical release

### P1: complete provider boundary
- Remove the temporary `DecoderOptions` compatibility overload.
- Move Japanese lattice, trie, morphology, correction, scorer and trace declarations out of the generic public `engine.hpp`.
- Migrate C API internals and evaluation tools to provider-neutral types.
- Verify static and shared builds plus C/Python integration.

### P2/P3: recognition quality
- Verify candidate recall independently from candidate ranking.
- Confirm word/POS/transition/cluster evidence provenance and avoid correlated double counting.
- Exercise long-distance context, topic switching, contradictions, question routing, temporal expiry and reassessment against sequential corpora.
- Tune policy on dev only; retain held-out test data.

### P4: complete Japanese surface realization
- Implement and test orthography, okurigana, inflection, punctuation, spacing, numerals, dates, times, units, currency, symbols and mixed-script output.
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

The v0.49 source and test changes are committed to the development branch. CI for this head, full golden-corpus parity, held-out quality, target-PC latency and Peak RSS have not yet been verified. Therefore this status does not mark practical operation as complete.

The separate SURGE repository and its development branches are not modified by this work.
