# kana-decoder Specification v0.86

## Paired beam-pruning evaluation

The corpus evaluation CLI supports two deterministic beam policies:

- Default: retain the best-scoring state for each rendered surface before filling the remaining beam slots.
- Legacy: score-only top-k pruning, selected with `--legacy-beam-pruning`.

`tools/evaluate_beam_ab.py` runs both policies against the same pack and the same
prepared corpus rows. It evaluates one explicitly selected UD Japanese-GSD split
(default: `test`), invokes the C++ evaluator twice, verifies that both runs
processed the same cases in the same order, and emits a JSON comparison report.

The report includes Top-1 exact accuracy, CER, decode failures, per-domain deltas,
counts of exact-match gains and regressions, and every case whose rendered output
changed. Deltas are calculated as diversity policy minus legacy policy. Therefore,
a positive exact-accuracy delta and a negative CER delta favor the diversity policy.
The report is a paired descriptive comparison, not a significance test.

## Corpus alignment and leakage safeguards

The UD-GSD adapter emits token rows grouped into contiguous dictionary-covered
segments. The A/B harness joins rows only within the same document and segment
identifier. Unknown-reading and out-of-dictionary tokens already break segments
in the adapter, so the harness cannot create false adjacency across excluded
tokens. Train, dev, and test rows are never mixed: exactly one split is selected
per run. Use dev for iteration and reserve test for final held-out comparison.

The upstream corpus and derived corpus remain local. Do not commit, publish, or
upload the UD-GSD source or generated evaluation TSV unless its license and
redistribution terms have been separately reviewed.

## Reproduction

After building `kana_decoder_eval` and preparing the UD-GSD TSV with
`tools/prepare_ud_gsd_corpus.py`, run:

```sh
python3 tools/evaluate_beam_ab.py \
  --evaluator build/kana_decoder_eval \
  --pack packs/ja-lite \
  --prepared-corpus build/ud_gsd_corpus.tsv \
  --split dev \
  --output build/beam-ab-dev.json
```

For the final held-out comparison, repeat with `--split test`. Both invocations
must use the same pack build, evaluator binary, and prepared corpus. Record the
pack identity, source commit, corpus manifest hashes, and exact command alongside
the JSON report when publishing results.

## Validation boundaries

CI unit tests cover segment joining, split isolation, paired metric deltas, and
the legacy CLI switch. They do not establish an accuracy gain. The current
`ja-lite` smoke tests remain integration checks only; they are not a substitute
for a full dictionary pack or the pinned UD Japanese-GSD evaluation. No production
beam policy change should be justified by synthetic fixtures alone.
