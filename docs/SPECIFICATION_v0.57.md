# kana-decoder: typed Japanese entities and SURGE graph integration v0.57

Version: 0.57.0
Parent: SPECIFICATION_v0.56.md

## 1. Generic LanguageAdapter entity contract

- Introduce provider-neutral `EntityTarget`: Number, Percent, Currency, Date, Time, Range, Unit and Identifier.
- `LanguageAdapter::make_entity` is the generic extraction contract.
- `NumericEntityTarget` remains an alias and `make_numeric_entity` remains a forwarding compatibility helper for numeric callers.
- The C API layout is unchanged.

## 2. Japanese entity extraction

The ja-JP adapter implements:

- Number, Percent, Currency and Unit through the existing bounded Japanese number parser.
- Date from year/month/day readings, including numeric day forms ending in `にち` and common special day readings (`ついたち` through `とおか`, `じゅうよっか`, `じゅうくにち`, `はつか`). Calendar validity, including leap years, is checked.
- Time with optional `ごぜん` / `ごご`, hour, minute (`ふん` / `ぷん`) and second (`びょう`) fields. Invalid hour/minute/second ranges are rejected.
- Range with `から`, decimal-scale alignment and optional unit.
- Identifier from an explicit sequence of Japanese digit readings. Leading zeroes are preserved in the rendered identifier.

Identifier extraction is an explicit adapter request. It is not automatically inferred from arbitrary prose because digit sequences can be ambiguous.

## 3. Surface realization

When numeric normalization is enabled, the Japanese renderer attempts full-reading Date, Time and Range recognition after validating the selected path's contiguous source coverage. If a typed entity is recognized, it renders the structured value; otherwise it retains the token-surface rendering path.

This stage recognizes a typed entity only when it spans the entire supplied reading. Partial-span extraction inside a longer sentence remains a follow-up task. The renderer remains non-generative for ordinary lexical/inflectional surfaces.

## 4. SURGE ContextGraph integration

- The runtime attempts full-reading Date, Time and Range extraction after UnifiedDecision has selected the interpretation path.
- A successful extraction is persisted as a reference-free Entity node plus a typed Evidence node in the same SemanticDelta as the selected Claims.
- Evidence supports the selected Claims; Claims have About relations to the Entity.
- Entity and Evidence are grouped into a bounded `NORMALIZATION_ENTITY` cluster, with temporal EvidenceValidity.
- Structured entity counts are included in runtime metrics and commit provenance.
- The same graph transaction is replayable through the shared EventJournal.

Structured entity extraction currently enriches the graph after UnifiedDecision. It does not yet contribute a separate scoring feature to the current recognition choice. That integration requires calibrated evidence-family weights and held-out evaluation.

## 5. Verification

- CI run #1042 passed: static/shared build, all 29 CTest cases, bounded decode and SURGE microbenchmarks, Japanese reference Pack verification, policy calibrator syntax check, and Python Pack/license policy tests.
- Runtime integration tests verify full-reading date rendering, typed Entity/Evidence graph insertion, metrics and journal recovery.
- These checks do not establish held-out CER, Candidate Recall@N, streaming commit quality, target-PC latency or Peak RSS. Those remain release gates.

## 6. Compatibility and limitations

- C API ABI remains unchanged.
- C++ LanguageAdapter gains the generic `make_entity` contract. The legacy numeric method remains as a forwarding helper.
- No changes are made to the upstream SURGE repository or the vendored SURGE Core snapshot.
- Partial-span date/time/range extraction, locale/calendar variants, relative dates, timezone handling, automatic identifier detection and entity-aware candidate rescoring remain future work.
