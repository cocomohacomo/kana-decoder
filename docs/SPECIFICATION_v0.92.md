# kana-decoder Specification v0.92: Real-corpus candidate-survival evaluation

## Purpose

Run the pinned UD Japanese-GSD plus Sudachi Small evaluation for the candidate-recall diagnostics branch, rather than relying only on synthetic fixtures in the regular C++ CI.

## Execution

The `Build Sudachi Small Pack` workflow is enabled for `work/candidate-recall-diversity-v066`. Its existing pipeline downloads the pinned UD Japanese-GSD source and pinned Sudachi dictionary, builds the normalized lexicon, prepares document-disjoint train/dev/test data, calibrates only on dev, and evaluates the held-out test split using the native sequential decoder.

The sequential evaluator emits:
- overall mutually exclusive candidate-survival outcomes;
- the same outcomes grouped by corpus domain;
- the same outcomes grouped by Unicode-codepoint reading-length bucket.

The grouped accounting must satisfy, independently for each group:
```
classified_gold_tokens == gold_tokens
unclassified_gold_tokens == 0
```
The sum of group gold-token counts must equal the overall gold-token count for each stratification.

## Evaluation discipline

- The UD Japanese-GSD source commit and dictionary source lock remain pinned.
- The dev split is used for calibration and policy selection.
- The test split is used only for final evaluation and diagnostic reporting.
- Grouped diagnostics are observational. They do not alter decoding, beam width, scoring, candidate caps, or SURGE runtime policy.
- Do not promote a policy based on test-split diagnostics.

## Resource and interpretation limits

The workflow retains its existing runtime memory gate of 1,792 MiB for the full Sudachi Pack evaluation. This is not the product target of 100 MiB. The 100 MiB gate remains limited to the bounded decode microbenchmark until a full production-pack memory reduction is separately implemented and measured.

The real-corpus run is considered validated only after the workflow completes successfully and the JSON output is inspected. A successful synthetic test does not substitute for this run.
