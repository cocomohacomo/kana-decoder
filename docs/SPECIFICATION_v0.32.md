# kana-decoder 仕様書 v0.32

- Version: 0.32.0
- Core license: MIT
- Runtime: CPU-only / offline
- Focus: dev-only calibration of native SURGE evidence-gated sequential decoding.

## 1. Purpose

v0.31 connected the native SURGE evidence gate to full-path C++ sentence decoding. The first Sudachi Pack measurement showed that the default margin of 0.5 produced exactly the morphology-only result. This indicates that the conservative gate did not activate often enough to affect the held-out corpus. v0.32 adds a reproducible calibration phase using only the development split.

## 2. Calibration policy

The native sequential evaluator supports two explicit modes:

- `dev-calibrate`: reads only rows whose split is `dev`, evaluates a bounded parameter grid, and writes the selected configuration to a transient TSV file.
- `test`: reads only rows whose split is `test`, loads the frozen calibration TSV, and evaluates the selected configuration once.

The dev grid contains 72 configurations:

- Context weight: 0.25, 0.5, 1.0, 2.0.
- Evidence margin: 0.0, 0.01, 0.05, 0.1, 0.25, 0.5.
- Additive smoothing alpha: 0.1, 0.5, 1.0.

Selection is deterministic: lowest Unicode-codepoint CER first, then higher sentence exact accuracy, then higher exact token alignment. If no calibrated SURGE configuration beats morphology-only on dev, the emitted configuration disables contextual contribution (weight 0.0) and uses the safe default alpha/margin. This avoids deploying a dev configuration that degrades the morphology baseline.

## 3. Smoothing API

`MaskedNgramScorer` now exposes:

- `set_smoothing_alpha(double)`: accepts finite values greater than 0 and at most 100; rejects invalid values without changing the current value.
- `smoothing_alpha()`: returns the active alpha.

The default remains 0.5, preserving existing scoring behavior. The selected alpha is applied to both unigram and contextual additive smoothing for the calibrated evaluation. No trained counts, calibration values, or new model files are embedded into the Language Pack.

## 4. Evaluation boundaries

- Training n-gram counts are generated from the train split only.
- Dev calibration uses gold tokenized rows only for sentence construction and post-decode alignment. Gold token boundaries are never supplied to the decoder.
- Test rows are not read during parameter selection. The selected configuration is frozen to a transient TSV before the test invocation.
- Test evaluation reports morphology-only, ordinary train-derived unigram + predicted-left context, and calibrated SURGE-gated left-context.
- Future/right context is not supplied to the native sequential decoder.
- The test result is not used to update the selected configuration.

This is full sentence reading-to-text decoding with gold sentence/token references for scoring, but it is not raw unsegmented-document evaluation or a live streaming latency benchmark.

## 5. Resource and compatibility constraints

- Candidate shortlist <=8, beam width <=8, expanded states <=262,144.
- Calibration reuses the same bounded decoder and scorer; it does not retain per-configuration lattices or paths between runs.
- Runtime default smoothing alpha remains 0.5; SURGE gate remains opt-in.
- No C ABI or Language Pack schema change.
- Calibration TSV and corpus data exist only in the CI workspace and are not committed or uploaded.

## 6. Validation requirements

CI must verify:
- Valid and invalid smoothing alpha handling.
- Exactly 72 calibration combinations are evaluated.
- Dev mode selects only dev rows and writes a readable configuration.
- Test mode reads the frozen configuration and scores only test rows.
- The full Sudachi Pack workflow measures calibration and test Peak RSS separately, each under 1,792 MiB.

No v0.32 accuracy gain is claimed until the new CI and full Pack workflow complete.
