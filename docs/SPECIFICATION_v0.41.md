# kana-decoder SURGE Normalization Specification v0.41

## 1. Purpose

v0.41 establishes the first in-process language-cognition foundation for SURGE-backed normalization. It extends the existing lexical, morphological, N-gram and cluster evidence with bounded typed objects for competing interpretations, semantic and syntactic relations, orthographic alternatives, structured entities, constraints, decisions, revisions and committed output spans.

This is an additive foundation. It does not yet claim full semantic parsing, automatic dependency parsing, or improved held-out CER. Existing decoder defaults, Language Pack format and C ABI remain unchanged.

## 2. Architecture

The intended flow is bidirectional rather than a one-way pipeline:

1. Reading/input stream produces bounded lexical and morphological candidates.
2. CandidateSet groups competing Hypotheses over a reading span.
3. SemanticFrame and SyntaxRelation attach predicate-role and dependency information.
4. Evidence and Constraint objects support comparison without collapsing uncertainty prematurely.
5. Decision records the selected interpretation and its evidence.
6. OrthographicHypothesis separates lexical meaning from output script/surface choice.
7. Entity carries typed numeric/date/time/range/unit/currency/identifier values.
8. Revision records later reinterpretation; CommitSpan records output that has crossed the commit boundary.

The generic SURGE Core owns state and provenance. The replaceable LanguageAdapter contract maps language-specific dictionary candidates into Hypotheses and converts recognized numeric readings into typed Entities. JapaneseLanguageAdapter provides the initial ja-JP implementation for candidate/POS mapping and bounded numeral parsing. Full syntax/semantic extraction, date/time phrase recognition and orthographic policy remain adapter extensions.

## 3. Bounded data model

The C++ implementation is in include/kana_decoder/surge_language.hpp and src/surge_language.cpp.

| Object | Capacity | Purpose |
|---|---:|---|
| Hypothesis | 32 | Competing interpretations |
| CandidateSet | 16, up to 8 candidates | Alternatives for one span |
| SemanticFrame | 16, up to 8 roles | Predicate and semantic roles |
| SyntaxRelation | 64 | Head/dependent relation |
| OrthographicHypothesis | 32 | Surface/script alternatives |
| Entity | 16 | Typed structured values |
| Evidence | 64, up to 4 dependencies | Provenance, confidence, validity and causal dependencies |
| Constraint | 32 | Morphological, syntactic, semantic, orthographic and policy restrictions |
| Decision | 32 | Selected hypothesis and evidence references |
| Revision | 32 | Reinterpretation history |
| CommitSpan | 16 | Non-overlapping committed output ranges |

Text is stored in fixed-size inline buffers. IDs are limited to 63 bytes, labels to 47 bytes and text to 191 bytes. Hypothesis spans use 16-bit code-point offsets. State insertion rejects capacity overflow, duplicate IDs, invalid spans, unresolved references, non-finite scores, invalid confidence and malformed typed entities.

LanguageState::memory_bytes() reports the fixed object footprint. It excludes caller-owned strings created by rendering functions and stack temporaries.

## 4. Evidence and provenance

Evidence is attached to a known Hypothesis and records its EvidenceKind, source, signed score, confidence, validity revision interval and up to four dependencies. Dependencies must reference Evidence already inserted into the state, preventing forward-reference cycles in the bounded append-only record set.

The initial aggregator groups evidence by kind and takes the strongest confidence-weighted contribution within each kind, then computes a weighted mean across observed kinds. This prevents multiple correlated observations in one channel from automatically dominating independent channels. Evidence outside its revision validity interval is ignored. Invalid or zero-total policies fail closed.

Decision evidence references must resolve to stored Evidence. Evidence kinds cover lexical, N-gram, POS, morphology, syntax, semantics, discourse, entities, orthography and user policy.

## 5. Entity normalization primitives

The initial renderer accepts typed entities, and the Japanese adapter now also parses a bounded subset of spoken Japanese integer/decimal readings. It is not a general speech recognizer or unrestricted natural-language number parser.

Supported kinds:
- Number
- Percent
- Currency
- Date
- Time
- Range
- Unit
- Identifier

Supported initial output rules:
- Japanese numeral reading to signed-64-bit mantissa plus up to 9 decimal places (digit names, 十/百/千/万/億/兆, common 300/600/800/3000/8000 sound changes).
- Arabic number output, optionally grouped by thousands, preserving explicitly supplied decimal scale.
- Japanese date form YYYY年M月D日.
- Japanese time form H時MM分 (minutes omitted when zero).
- Range form start〜end + unit.
- Unit and percent suffix output.
- Yen suffix output; other currency symbols/codes are prefixed.

Invalid calendar dates, invalid clock times, reversed ranges and missing required units fail closed. Kanji numeral output, negative-number words, fractions, counters, locale-specific decimal handling, contextual unit recognition and broad date/time phrase parsing remain language-adapter work.

## 6. Language Adapter API

LanguageAdapter is a replaceable C++ interface with a language tag, candidate-to-Hypothesis mapping and typed numeric-entity construction. The initial Japanese adapter preserves dictionary candidate identity, POS interpretation and signed lexical/connection cost. It also maps parsed number readings to Number/Percent/Currency/Unit entities. It deliberately does not infer semantic roles or syntax from POS alone.

This keeps the generic state model language-independent while allowing future language packs to supply grammar, morphology, entity parsing and script policy without changing SURGE Core.

## 7. Punctuation policy

terminal_punctuation maps a discourse-act classification to Japanese, ASCII or minimal terminal punctuation. It does not infer discourse acts. Question/statement/exclamation recognition remains an upstream discourse task.

## 8. Decision and revision semantics

A Decision must select a known Hypothesis. If compared hypotheses are supplied, the selected hypothesis must be among them. Confidence is finite and in [0,1]. Evidence IDs are bounded opaque references and must be non-empty.

A Revision must identify its target, replacement hypothesis and reason. The previous hypothesis may be empty for an initial interpretation. The initial implementation stores records but does not automatically mutate or invalidate downstream dependencies; dependency propagation is a later phase.

CommitSpan ranges must be valid and non-overlapping. The initial API records commit metadata but does not modify StreamingSession output. Streaming integration and rollback/revision policy remain a later phase.

## 9. JSON Schema

The exchange shape and array ceilings are documented in docs/schemas/surge-language-state.schema.json. The C++ fixed-capacity state is authoritative for runtime limits. The schema describes the portable interchange envelope, not a persistence format commitment.

## 10. Resource and compatibility guarantees

- C++17; no external runtime dependency.
- Fixed-size arrays and inline text buffers in LanguageState.
- No unbounded graph traversal or recursive inference.
- Existing decoder and pack defaults remain unchanged.
- New module is linked into static and shared native libraries.
- No new fields are added to the Language Pack.
- No change to existing C API or Python API.
- Capacity overflow and invalid references fail closed.

## 11. Validation

The dedicated surge_language_foundation_tests target covers:
- Hypothesis and CandidateSet references.
- SemanticFrame and SyntaxRelation references.
- Orthographic alternatives.
- Numeric, currency, date and time rendering.
- Invalid dates and invalid spans.
- Decision, Evidence provenance/expiry and Revision validation.
- CommitSpan overlap rejection.
- Fixed-capacity overflow.
- Punctuation policy variants.

## 12. Evidence-aware N-best reranking

Hypothesis may bind a language candidate ID in addition to its span and surface. The optional rerank_paths API matches returned N-best tokens to these hypotheses using exact span/candidate-ID/surface identity, computes revision-valid evidence aggregates and reorders at most eight paths. It normalizes the decoder path score by token count before applying the configured evidence weight. The function does not modify path scores, candidate generation, or the default decode path. The evidence weight must be calibrated on dev data and frozen before held-out evaluation.

The measured test run motivating this API contained 1,631 tokens whose gold candidate appeared in N-best but not in the best path. The reranker is a capability for testing evidence-supported recovery, not an assertion that these gold tokens can be recovered at inference time. Only hypotheses with actual evidence are eligible for the evidence adjustment; unsupported alternatives are not assigned synthetic evidence.

## 13. Next implementation stages

1. Connect actual Word/POS/Cluster/Semantic/Syntax evidence producers to Hypothesis records and calibrate path-level reranking on dev, then evaluate once on held-out test.
2. Dependency-aware invalidation/retraction and persistence of Evidence/Decision/Revision.
3. Japanese language adapter: extend number-word grammar, unit/date/time/range recognition and script policy.
4. Syntax/semantic adapter using dictionary POS, inflection and connection data.
5. StreamingSession integration for provisional normalization, revision and commit.
6. Train/dev/test evaluation of semantic/orthographic gains with causal input and strict memory/RTF budgets.

No production accuracy gain is claimed by the v0.41 foundation alone.
