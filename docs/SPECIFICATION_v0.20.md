# kana-decoder 仕様書 v0.20

- Version: 0.20.0
- Core license: MIT
- Runtime: CPU-only / offline
- Development priority: complete functional Japanese conversion before optional size optimization. 30 MiB is not a development hard limit.

## 1. Sudachi V1 source and import

- SudachiDict 20260723 V1 raw lexicon uses the explicit \`/sudachidict-raw/v1/20260723/\` endpoint and pinned SHA-256 \`e49936daef64043657752eb9f4ada912cf0316e26dd158a6200c770e2f93e706\`.
- Header is named CSV; \`Index_Form\` is lookup surface and \`Headword\` is display surface.
- V1 composition-only entries may use Left_Id or Right_Id = -1. Validation accepts exactly -1 and importer excludes those rows as standalone candidates.
- Lexicon and matrix imports use bounded temporary SQLite storage.

## 2. First production Pack facts

- Pack ID: \`ja-jp-sudachi-small\`, version \`20260723.1\`.
- Final entries: 754,602.
- Matrix: 5,981 x 5,981; 35,759,450 nonzero cells.
- Installed size: 693,542,002 bytes (661.413 MiB).
- Artifact ZIP: 149,644,215 bytes in workflow run 44374465962.
- No training corpus supplied; ngram.tsv has zero learned n-gram records.

## 3. Native runtime acceptance

- Workflow run 44374465962 completed successfully after building the C++ runtime and loading the full production Pack.
- Four-case smoke evaluation produced no decode failures.
- Combined Pack load plus evaluation time: 8.00 seconds on one hosted CI runner.
- Peak RSS: 1,425,240 KiB (about 1.36 GiB) on that runner.
- Smoke exact accuracy: 25%; CER: 1.0. This four-row corpus is a wiring check only, not a representative accuracy benchmark.
- Evaluator supports \`--details\` to print per-case expected/predicted outputs and Pack load milliseconds. Detailed diagnostics are required before changing scoring or dictionary mapping.

## 4. Remaining quality gates

- Diagnose each smoke mismatch using per-case predictions.
- Build representative train/dev/test Japanese corpora split by document/speaker/source to prevent leakage.
- Add language-appropriate n-gram training data and evaluate exact accuracy/CER by domain.
- Measure p50/p95/p99 latency and peak RSS on the intended target CPU/RAM environment.
- Only after these measurements decide whether to optimize the 661 MiB Pack or connection matrix representation.
