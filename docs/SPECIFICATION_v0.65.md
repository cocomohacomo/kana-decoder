# kana-decoder: Safe SURGE fallback and margin calibration v0.65

Version: 0.65.0
Parent: SPECIFICATION_v0.64.md

## 1. Runtime safety semantics

- A SURGE decision may select, defer, or fail.
- When the evaluator receives a defer or runtime-open failure and a Decoder Top-1 path exists, the effective output is the Decoder Top-1 surface. A defer is not scored as an empty string or a full-reference deletion.
- A decode failure with no candidate path remains an actual failure and is reported separately.
- Evaluation distinguishes selected SURGE outputs, Top-1 fallbacks, and decode failures. These three outcomes must partition all input cases.
- Candidate-available diagnostic classes continue to distinguish runtime failure, deferred decision, SURGE exact, and SURGE wrong.

## 2. Confidence margin guard

- `SurgeRuntimeConfig.minimum_recognition_margin` is passed to SURGE Core's `DecisionPolicy.minimum_margin`.
- The guard is finite, non-negative, and bounded to 1,000. Invalid values reject runtime configuration.
- The dev-only calibrator evaluates the existing evidence-family presets plus minimum-margin guards 0.25, 0.50, and 1.00.
- Selection remains minimum effective dev CER, then maximum sentence exact, then fixed preset order.
- The selected margin is persisted in the policy TSV and provenance report. The held-out test split is evaluated once and never used to choose a margin.

## 3. Workflow reliability

- The pinned Sudachi/GSD job timeout is increased from 60 to 120 minutes because the full build, dev calibration grid and held-out evaluation are sequential.
- Existing memory gates remain unchanged at 1,792 MiB. Hosted runner measurements are not target-device claims.

## 4. Interpretation

- Top-1 fallback makes defer behavior operationally safe, but does not guarantee that every selected SURGE override improves accuracy.
- Margin calibration is a confidence-control experiment, not a proof that score margins are calibrated probabilities.
- Candidate-generation defects remain a separate workstream. Token-aligned miss attribution must be used to decide whether to improve lexicon coverage, morphology, or span construction.
- The pinned UD Japanese-GSD benchmark is not a substitute for conversational-domain evaluation.

## 5. Completion gate

- Unit tests must verify low-margin defer behavior.
- Calibration tests must verify the margin is emitted and all diagnostic partitions remain fail-closed.
- CI and the pinned real-corpus workflow must complete successfully before this version is considered validated.
