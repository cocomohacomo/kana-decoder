# kana-decoder: typed-entity evidence calibration v0.59

Version: 0.59.0
Parent: SPECIFICATION_v0.58.md

## 1. Purpose

Make the typed_entity Evidence family introduced in v0.58 part of the deterministic SURGE policy calibration process. The evidence is optional and must be measured against the same held-out test set before it is enabled as a production preference.

## 2. Calibration policy

- Add typed_entity to the recognized family list emitted by tools/calibrate_surge_policy.py.
- Preserve all existing policy presets and add three controlled alternatives:
  - disabled: 0.0
  - conservative: 0.5
  - strong: 1.5, combined with the existing context-oriented profile.
- The baseline/default value remains 1.0.
- The renderer multiplier remains an independent policy dimension.
- The calibrator invokes the evaluator with split dev only. It must never read the test split.
- Every preset must return a non-empty dev set, zero runtime failures, and a measured Peak RSS not exceeding the configured limit (default 1,792 MiB).
- Selection remains deterministic: minimum CER, then maximum sentence exact, then fixed preset order.
- The chosen policy and provenance hashes are written to the calibration report.

## 3. Real-data observability

The SURGE real-data evaluator now aggregates and reports four typed-entity counters inside surge_core: detected entities, pre-decision evidence observations, selected-path typed-entity observations, and persisted typed entities. The calibrator copies these counters into each candidate row so weight selection can be interpreted alongside the actual evidence exposure. Older evaluator JSON remains accepted with zero defaults for these optional counters.

## 3. Test coverage

The fake evaluator test verifies:
- the evaluator receives only dev;
- all eight fixed presets are evaluated;
- typed_entity is present in each generated policy;
- the deterministic best preset is selected from its reported dev metrics;
- the selected TSV contains the typed-entity multiplier;
- source-lock provenance is hashed.

The baseline smoke policy fixture explicitly includes typed_entity=1.0.

## 4. Verification boundary

This change expands calibration coverage only. It does not establish that 0.5, 1.0 or 1.5 is optimal. The strong preset is a candidate for dev comparison, not a production recommendation.

The pinned Sudachi Small + UD Japanese-GSD workflow must complete to produce actual dev calibration and held-out test metrics. Test data remains frozen and is evaluated only after the dev-selected policy has been written. Report CER, sentence exact, overrides, deferrals, runtime failures, decode failures, elapsed time and Peak RSS for every test comparison.

No held-out CER, Candidate Recall@N, streaming commit accuracy, target-PC latency or Peak RSS improvement is claimed in this specification.

## 5. Scope exclusions

- No changes are made to the separate SURGE repository.
- No changes are made to vendored SURGE Core.
- No new entity grammar is introduced.
- No negative evidence is added for candidates that do not align with typed entities.
- No policy weight is promoted to production default without held-out evidence.
