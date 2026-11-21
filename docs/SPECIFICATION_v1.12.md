# kana-decoder Specification v1.12: Extended dev-only recognition-margin calibration

Parent: SPECIFICATION_v1.11.md

## 1. Motivation

The pinned Sudachi Small evaluation showed that candidate availability is not the only constraint: SURGE overrides can be harmful even when a viable candidate exists. The existing calibration grid tested recognition margins through 1.00. This revision extends the dev-only sweep to 1.50, 2.00, and 3.00 so that conservative abstention can be evaluated rather than assumed.

## 2. Change

`tools/calibrate_surge_policy.py` adds three deterministic presets:
- `margin_guard_150`: minimum recognition margin 1.50;
- `margin_guard_200`: minimum recognition margin 2.00;
- `margin_guard_300`: minimum recognition margin 3.00.

All evidence-family weights remain 1.0 and normalization rendering remains disabled for these presets. Existing presets and deterministic tie-breaking order are unchanged.

## 3. Evaluation contract

- All candidate policies are evaluated on the pinned development split only.
- Selection remains minimum dev CER, then maximum dev sentence exact, then fixed preset order.
- Held-out test is not used to choose a margin or any other parameter.
- The selected policy is applied once to held-out test by the existing workflow.
- The calibration report records the selected margin, all preset results, source-lock digest, corpus digest, and calibrator digest.
- The regression test verifies the expanded margin grid and dev-only invocation.

## 4. Interpretation

A higher margin is not presumed better. It is useful only if dev evaluation demonstrates that the more conservative policy reduces harmful overrides without unacceptable loss of beneficial overrides. If the selected preset remains unchanged, the additional sweep provides evidence that the tested wider margin range did not improve the dev objective.

## 5. Constraints

- No change to decoder beam width, candidate caps, lattice limits, or runtime memory bounds.
- No change to SURGE Core or vendored `third_party/surge`.
- No raw corpus traces are committed or uploaded.
- PR #2 remains Draft and unmerged.
