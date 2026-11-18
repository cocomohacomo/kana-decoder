# kana-decoder 仕様書 v0.21

- Version: 0.21.0
- Core license: MIT
- Runtime: CPU-only / offline
- Development priority: correctness, reproducible evaluation and functional completion before optional size optimization. 30 MiB is not a development hard limit.

## 1. Sudachi V1 production Pack

- SudachiDict 20260723 V1 raw lexicon uses the explicit `/sudachidict-raw/v1/20260723/` endpoint and pinned SHA-256 `e49936daef64043657752eb9f4ada912cf0316e26dd158a6200c770e2f93e706`.
- V1 composition-only entries may use Left_Id or Right_Id = -1. They are excluded as standalone candidates.
- Pack ID: `ja-jp-sudachi-small`, version `20260723.1`.
- Final entries: 754,602. Connection matrix: 5,981 x 5,981, with 35,759,450 nonzero cells.
- Expanded Pack: 693,542,002 bytes (661.413 MiB). The 30 MiB target remains deferred.

## 2. Runtime baseline and quality status

- Production Pack native load succeeded in workflow run 44374465962.
- Hosted-runner combined load/evaluation: 8.00 seconds; peak RSS: 1,425,240 KiB. These are single-run reference measurements, not target-device guarantees.
- Four-case smoke: exact 1/4 (25%), CER 1.0, decode failures 0. This is a wiring check only, not representative accuracy.
- No learned n-gram records were included because no training corpus was supplied.

## 3. Evaluation CLI architecture

- `tools/evaluate_corpus.cpp` separates TSV parsing, UTF-8 validation, Unicode-codepoint conversion, Levenshtein distance, case evaluation, metric accumulation and JSON serialization into dedicated functions and data structures.
- The evaluator retains the existing TSV contract: `reading<TAB>expected[<TAB>domain]`; a missing domain maps to `unspecified`.
- CRLF rows are normalized at the domain boundary. Invalid rows report their source line number and exit with status 2.
- JSON escaping now preserves all JSON control characters through standard escapes or `\\u00XX`; valid UTF-8 bytes remain unchanged.
- Aggregate metrics are computed identically in normal and `--details` modes. Detailed mode adds per-case reading, expected, predicted, domain, edit distance and decode-failure status.
- Domain aggregation is deterministic (lexicographic key order).

## 4. Regression gates

- `tests/test_evaluator_cli.py` exercises both CLI modes, parses emitted JSON, verifies that detailed and aggregate metrics agree, checks CRLF normalization, and confirms malformed/empty corpora fail closed.
- CTest registers this as `evaluator_cli_contract_tests`.
- A successful CI run is required before this refactor is considered verified.

## 5. Remaining quality gates

- Production run 44375182255 detailed the four smoke predictions: あめ→アメ (expected 雨), はし→橋 (correct), きしゃ→記者 (expected 汽車), ぴ→日 (expected ぴ). The tiny corpus remains diagnostic only.
- Do not infer an importer or matrix defect from these four examples. Add candidate-level traces for word cost, connection cost, transition cost and context score; inspect unknown fallback activation; then evaluate with disjoint, representative data.
- Build representative train/dev/test corpora split by document, speaker or source to prevent leakage.
- Add language-appropriate n-gram training data and report exact accuracy/CER by domain.
- Measure p50/p95/p99 latency and peak RSS on the intended target CPU/RAM environment.
- Only after correctness and target measurements decide whether to optimize the 661 MiB Pack or connection-matrix representation.
