# kana-decoder Specification v0.91: Stratified candidate-survival diagnostics

## Purpose

Extend the exhaustive candidate-survival accounting from v0.90 with stratification. Aggregate totals can hide domain-specific and input-length-specific failure modes, so the evaluator now emits the same mutually exclusive outcome accounting at three levels:

- Overall test split: `candidate_survival_diagnostic`.
- Corpus domain: `candidate_survival_by_domain`.
- Reading-length bucket: `candidate_survival_by_reading_length`.

## Domain integrity

Each sentence is assigned the domain in the token-corpus row. All rows belonging to the same document/sentence key must use the same domain; inconsistent metadata rejects the corpus rather than silently splitting one sentence across groups.

Domain labels are emitted as JSON string keys with quoting/escaping.

## Reading-length buckets

Length is measured in Unicode code points of the concatenated kana reading:

- `1-4`
- `5-8`
- `9-16`
- `17-32`
- `33-64`
- `65-128`
- `129-256`

Empty readings and readings longer than the evaluator's existing 256-code-point limit remain out of scope, as before.

## Invariants

For each group independently:

```
classified_gold_tokens == gold_tokens
unclassified_gold_tokens == 0
```

Across all groups in either stratification:

```
sum(group.gold_tokens) == overall.gold_tokens
```

All outcome fields retain the v0.90 definitions, including the successful terminal category `final_selected`. Grouping is diagnostic only: it does not change decoder policy, training, scoring, beam width, language-pack contents, or SURGE runtime behavior.

## Interpretation

Use domain groups to identify domain-sensitive dictionary or scoring gaps, and reading-length groups to identify failures that grow with longer input. Do not tune on the held-out test split; use development data for selecting any policy changes and reserve test data for final reporting.
