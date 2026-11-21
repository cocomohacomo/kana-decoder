# kana-decoder Specification v0.99: Paired sentence outcome diagnostics

## Scope

This revision extends v0.98 quality-linked candidate bottleneck attribution with case-level paired outcomes. It is evaluation-only and does not change production decoding, ranking, candidate generation, or policy selection.

## New report fields

- `surge_paired_sentence_outcomes`: overall counts under the `all` group
- `surge_paired_sentence_outcomes_by_domain`
- `surge_paired_sentence_outcomes_by_reading_length`

Each sentence is decoded once with the morphology-only baseline and once with the selected SURGE policy for this diagnostic. The comparison uses the same gold reference and Unicode codepoint edit distance.

The nine mutually exclusive outcome labels form the Cartesian product of:
- CER: `cer_improved`, `cer_tied`, `cer_regressed`
- Exact match: `exact_improved`, `exact_tied`, `exact_regressed`

A CER change is based on the sentence's edit-distance count against the same reference, so the comparison is equivalent to per-sentence CER ordering. Exact match is assessed independently. This preserves mixed outcomes where CER and exact match move in opposite directions.

Empty readings and readings longer than 256 codepoints are excluded from these paired strata, consistent with the existing reading-length diagnostic limits. Domain groups include every eligible sentence; reading-length groups partition only eligible nonempty readings.

## Interpretation and safety

These are descriptive paired outcomes, not causal explanations. They complement aggregate CER and exact accuracy and help expose cases hidden by averages. The outcome groups are not used to select a policy or modify decoder behavior. Any future intervention must be evaluated as a controlled ablation using development data only. Held-out test data remains report-only.

## Resource behavior

Counts are aggregated online into fixed-size maps keyed by domain and reading-length bucket. No sentence-level predictions or outcome records are retained. The diagnostic performs two additional decode calls per eligible sentence in the offline evaluator; it does not add work to production inference. The existing full-Pack memory ceiling is 1792 MiB in the current CI workflow; the 100 MiB limit applies only to the bounded decode microbenchmark, not the full Japanese Pack.

## Integrity tests

Tests verify that all nine outcome counts partition each group's case count, overall outcomes match the eligible SURGE evaluation case count, domain groups cover the overall eligible set, and reading-length groups do not include excluded inputs.
