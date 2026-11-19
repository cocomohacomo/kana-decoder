# kana-decoder: partial-span typed entities and SURGE pre-decision evidence v0.58

Version: 0.58.0
Parent: SPECIFICATION_v0.57.md

## 1. Provider-neutral extraction contract

- Add `EntitySpan`, containing a code-point `Span`, typed `Entity`, and rendered surface.
- Add bounded `LanguageAdapter::extract_entities(reading, max_entities, output)`.
- The default adapter implementation returns an empty result, preserving compatibility for providers that do not implement typed extraction.
- Japanese adapter limits extraction to 16 entities, 128 code points per entity, and a 1,024-byte UTF-8 reading input. The SURGE runtime itself continues to cap a reading at 256 code points.
- Returned spans are ordered, non-overlapping and use UTF-8 code-point offsets, not byte offsets.

## 2. Japanese partial-span extraction

- Search within longer Japanese readings for complete Date, Time and Range expressions.
- Date recognition uses the existing calendar validation and Japanese day-reading rules.
- Time recognition supports optional AM/PM, hour, minute and second forms.
- Range recognition supports decimal-scale alignment and optional unit only when explicitly supplied to the adapter. Automatic unit inference is not performed.
- Extraction is bounded and fail-closed on invalid UTF-8, invalid limits or invalid input length.
- The extractor does not guess arbitrary identifiers or reinterpret ordinary prose as a numeric entity.

## 3. Surface realization

- When numeric normalization is enabled, the renderer extracts typed spans before composing output.
- A typed entity replaces a region only when the selected token path exactly and contiguously covers the entity span on token boundaries.
- Tokens outside typed spans retain their selected dictionary surfaces. The renderer does not synthesize lexical or inflectional forms.
- If token boundaries do not align with an entity span, the typed replacement is skipped and the original token-surface path is retained.

## 4. SURGE pre-decision evidence

- Extract typed entities before `UnifiedDecision`.
- A candidate path receives `typed_entity` evidence only if its token boundaries exactly cover an entity span and concatenated candidate surfaces exactly match the entity's normalized surface.
- Each aligned entity contributes a bounded positive observation (value 2.0, evidence weight 1.0) correlated by entity span. The existing `recognition_family_weights["typed_entity"]` calibration can scale or disable this family.
- No negative evidence is assigned to paths that do not match, because absence of a normalized dictionary surface is not proof of semantic incorrectness.
- Metrics expose detected entities, added typed-entity observations, and observations accepted by the selected path.

This is a conservative pre-decision integration. It rewards explicit agreement with a recognized typed entity and avoids penalizing alternate surfaces solely because a dictionary lacks the normalized spelling. The evidence-family weight must be calibrated on development data before production use.

## 5. SURGE ContextGraph persistence

- Persist all extracted Date, Time and Range spans in the same SemanticDelta as selected Claims and normalization commit/proposal.
- Each Entity is accompanied by typed Evidence and temporal EvidenceValidity.
- Supports/About relations are attached only to selected Claims whose spans are contained in the typed entity span. Unrelated sentence Claims are not linked to that entity.
- Each entity/evidence pair is placed in a bounded `NORMALIZATION_ENTITY` cluster partitioned by source digest, span and type.
- Journal recovery remains transactional through the existing EventJournal.

## 6. Verification and release limits

New conformance cases cover:
- Two typed entities (date and time) inside one longer sentence.
- Correct UTF-8 code-point span offsets.
- Partial-span rendering while preserving surrounding particles and words.
- A lower-ranked decoder path being selected through typed-entity pre-decision evidence.
- Entity persistence and evidence metrics.

CI must pass static/shared build, CTest, bounded microbenchmarks, Japanese reference Pack verification and Python policy tests before this version is considered verified.

No held-out CER, Candidate Recall@N, streaming commit accuracy, target-PC latency or Peak RSS improvement is claimed without dedicated measurements. Entity extraction currently covers Date, Time and Range, not arbitrary unit/currency spans or relative dates.

## 7. Compatibility

- C API ABI and existing LanguagePack interface remain unchanged.
- The new extraction method has a default empty implementation for adapters that do not support it.
- The separate SURGE upstream repository and vendored SURGE Core are not modified.
