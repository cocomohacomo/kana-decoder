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

The generic SURGE Core owns state and provenance. Language adapters own Japanese grammar, dictionary lookup, reading conversion, number-word parsing and orthographic conventions.

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
| Constraint | 32 | Morphological, syntactic, semantic, orthographic and policy restrictions |
| Decision | 32 | Selected hypothesis and evidence references |
| Revision | 32 | Reinterpretation history |
| CommitSpan | 16 | Non-overlapping committed output ranges |

Text is stored in fixed-size inline buffers. IDs are limited to 63 bytes, labels to 47 bytes and text to 191 bytes. Hypothesis spans use 16-bit code-point offsets. State insertion rejects capacity overflow, duplicate IDs, invalid spans, unresolved references, non-finite scores, invalid confidence and malformed typed entities.

LanguageState::memory_bytes() reports the fixed object footprint. It excludes caller-owned strings created by rendering functions and stack temporaries.

## 4. Entity normalization primitives

The initial renderer accepts already-recognized typed entities; it is not a kana-to-number parser.

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
- Arabic number output, optionally grouped by thousands.
- Japanese date form YYYY年M月D日.
- Japanese time form H時MM分 (minutes omitted when zero).
- Range form start〜end + unit.
- Unit and percent suffix output.
- Yen suffix output; other currency symbols/codes are prefixed.

Invalid calendar dates, invalid clock times, reversed ranges and missing required units fail closed. Japanese kanji numeral parsing, locale-specific decimal handling and contextual unit disambiguation remain language-adapter work.

## 5. Punctuation policy

terminal_punctuation maps a discourse-act classification to Japanese, ASCII or minimal terminal punctuation. It does not infer discourse acts. Question/statement/exclamation recognition remains an upstream discourse task.

## 6. Decision and revision semantics

A Decision must select a known Hypothesis. If compared hypotheses are supplied, the selected hypothesis must be among them. Confidence is finite and in [0,1]. Evidence IDs are bounded opaque references and must be non-empty.

A Revision must identify its target, replacement hypothesis and reason. The previous hypothesis may be empty for an initial interpretation. The initial implementation stores records but does not automatically mutate or invalidate downstream dependencies; dependency propagation is a later phase.

CommitSpan ranges must be valid and non-overlapping. The initial API records commit metadata but does not modify StreamingSession output. Streaming integration and rollback/revision policy remain a later phase.

## 7. JSON Schema

The exchange shape and array ceilings are documented in docs/schemas/surge-language-state.schema.json. The C++ fixed-capacity state is authoritative for runtime limits. The schema describes the portable interchange envelope, not a persistence format commitment.

## 8. Resource and compatibility guarantees

- C++17; no external runtime dependency.
- Fixed-size arrays and inline text buffers in LanguageState.
- No unbounded graph traversal or recursive inference.
- Existing decoder and pack defaults remain unchanged.
- New module is linked into static and shared native libraries.
- No new fields are added to the Language Pack.
- No change to existing C API or Python API.
- Capacity overflow and invalid references fail closed.

## 9. Validation

The dedicated surge_language_foundation_tests target covers:
- Hypothesis and CandidateSet references.
- SemanticFrame and SyntaxRelation references.
- Orthographic alternatives.
- Numeric, currency, date and time rendering.
- Invalid dates and invalid spans.
- Decision and Revision validation.
- CommitSpan overlap rejection.
- Fixed-capacity overflow.
- Punctuation policy variants.

## 10. Next implementation stages

1. Path-level N-best trace and candidate recovery ranking, guided by measured 1,631 recoverable test tokens.
2. Evidence provenance IDs and dependency-aware validity/retraction.
3. Japanese language adapter: number-word parser, unit/date/time/range recognition and script policy.
4. Syntax/semantic adapter using dictionary POS, inflection and connection data.
5. StreamingSession integration for provisional normalization, revision and commit.
6. Train/dev/test evaluation of semantic/orthographic gains with causal input and strict memory/RTF budgets.

No production accuracy gain is claimed by the v0.41 foundation alone.
