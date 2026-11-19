# kana-decoder: dictionary-backed Japanese inflection and okurigana v0.52

Version: 0.52.0
Parent: SPECIFICATION_v0.51.md

## 1. Goal

Preserve Japanese conjugation and okurigana exactly as supplied by the licensed dictionary entry selected by SURGE. The renderer must not reconstruct an inflected form from a lemma using heuristic suffix rules.

## 2. Dictionary contract

The normalized lexicon already carries:
- reading key;
- output surface;
- lemma/reference form;
- POS;
- candidate identity and lexical/connection costs;
- inflection type;
- inflection form.

For Sudachi V1 CSV:
- `Headword` is the written headword used as the output surface;
- `POS5` is the conjugation type;
- `POS6` is the conjugation form;
- `Dictionary_Form` is the dictionary-form reference information.

The importer preserves POS5/POS6 as inflection metadata and converts the `*` empty-value sentinel to an empty field. Explicit `Conjugation_Type`/`Conjugation_Form` or `Inflection_Type`/`Inflection_Form` aliases may override the POS5/POS6 values in compatible named CSVs.

## 3. Surface realization policy

- The selected dictionary candidate surface is authoritative for orthography and okurigana.
- The renderer preserves that exact surface, including conjugated endings and sound-change forms.
- The renderer does not synthesize an inflection from a lemma, strip okurigana, or append a guessed suffix.
- If a required inflected surface is absent from the dictionary candidate set, it remains a candidate-generation/dictionary-coverage issue. The renderer must not silently invent a form.
- SURGE UnifiedDecision remains the only authority selecting the candidate surface. Rendering occurs after selection.

## 4. Conformance

Added tests verify:
- Sudachi V1 POS5/POS6 are retained as conjugation type/form;
- ordinary `*` sentinels become empty metadata;
- explicit named conjugation aliases are preserved;
- dictionary-backed `回っ` (五段-ラ行 / 連用形-促音便) survives decode and render unchanged;
- dictionary-backed `食べて` (下一段-バ行 / 連用形-一般) survives decode and render unchanged.

## 5. Non-goals

This version does not add a rule-based inflector. It intentionally relies on dictionary coverage of each surface form to avoid incorrect okurigana or conjugation. A future rule-based inflector would require explicit validated conjugation paradigms, lemma linkage, source evidence and SURGE-visible alternatives.

## 6. Verification

Source and tests are committed to the development branch. CI, golden-corpus parity, held-out CER, Candidate Recall@N, target-device latency and Peak RSS remain unverified until measured on the final PR head.

## 7. Remaining practical-release requirements

- Expand licensed Japanese dictionary coverage for inflected forms and lemma relations.
- Measure inflected-form Candidate Recall@N separately from candidate ranking.
- Add multi-token inflection chains (auxiliaries, particles, contractions, sound changes) to held-out evaluation.
- Complete date/time/unit/currency/range/identifier Entity recognition and evidence-backed surface alternatives.
- Run held-out CER, sentence exact match, streaming commit quality, p95 latency and Peak RSS on the target edge PC.
- Verify clean offline installation and stable external ABI.
