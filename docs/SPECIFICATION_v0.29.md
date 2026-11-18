# kana-decoder 仕様書 v0.29

- Version: 0.29.0
- Core license: MIT
- Runtime: CPU-only / offline
- Focus: real Japanese corpus, held-out context ablation, no corpus redistribution.

## 1. Evaluation corpus

Use UniversalDependencies/UD_Japanese-GSD, pinned to commit `c662dc96ed5ee5ac0fc8b5e44261557e431f50e6`.

- Upstream: https://github.com/UniversalDependencies/UD_Japanese-GSD
- License: CC BY-SA 4.0 for treebank annotations. The upstream notice distinguishes annotation licensing from underlying sentence text; this evaluation is benchmark-only and does not redistribute the source data.
- The three official train/dev/test CoNLL-U files and LICENSE.txt are downloaded by CI at runtime. They are not committed or uploaded as artifacts.
- The source is used only for offline evaluation. No corpus-derived n-gram model or trained Pack is published.
- The dataset contains news/blog text and is not a balanced sample of all Japanese usage. Results must be reported as domain-specific benchmark metrics.

## 2. Corpus adapter

`tools/prepare_ud_gsd_corpus.py` converts CoNLL-U to the project token TSV.

- Uses the case-insensitive `MISC.UnidicInfo` key (upstream spelling). In actual GSD rows, the fifth comma-separated field (index 4) carries the token-form reading, including inflected forms; the adapter prefers it, then accepts the fourth or first field only when kana-like. Katakana is converted to hiragana.
- Also accepts explicit `Reading` or `Pron` MISC fields.
- Punctuation/symbol/number-only tokens may use their surface form when no pronunciation is present.
- A token is retained only when its exact (hiragana reading, surface) pair exists in the normalized Sudachi lexicon.
- Missing-reading or out-of-dictionary tokens break the sentence into contiguous covered runs. The adapter never joins tokens across an unknown token, preventing false n-gram adjacency.
- Retains official train/dev/test splits. Each source sentence is a document; contiguous runs receive distinct sentence IDs.
- Writes a source manifest with pinned commit, per-file SHA-256, token coverage, excluded tokens, and license metadata.

CLI:

    python tools/prepare_ud_gsd_corpus.py --input-dir work/ud-gsd --lexicon work/lexicon.tsv --output work/tokenized.tsv --source-commit c662dc96ed5ee5ac0fc8b5e44261557e431f50e6

The resulting TSV is passed through the existing document-disjoint `prepare_context_corpus.py` compiler to create train sequences and held-out gold files.

## 3. Token-level contextual ablation

`tools/evaluate_context_ablation.py` compares three scoring policies on the held-out test split:

1. Morphology-only: negative dictionary word cost and candidate-local connection cost. Full matrix transition costs are intentionally excluded from this token-local ablation; they are evaluated in full-path decoder tests.
2. Combined: morphology score plus unigram prior and left/right masked bigram scores. The smoothed probability equations and alpha=0.5 mirror `MaskedNgramScorer` in `src/scoring.cpp`.
3. SURGE evidence-gated: rank candidates by contextual evidence only, and resolve only when at least two candidate surfaces have observed (non-zero count) contextual support and the best-vs-second supported context margin is at least 0.5. Otherwise abstain.

- Candidate generation is approximated by all dictionary entries for the reading, sorted by word cost and candidate ID, capped at the runtime limit of eight.
- Multiple morphological candidate IDs with the same surface are collapsed by best score.
- Candidate coverage, morphology top-1 accuracy, combined top-1 accuracy, SURGE resolution count, resolution precision, correct-resolution recall over covered tokens, and abstention are reported.
- The test gold token is used only for accuracy. Left/right neighboring token IDs are teacher-forced gold context. This isolates the scoring contribution and is an oracle-context upper bound, not end-to-end conversion accuracy.
- The evaluation is token-local; it does not claim that the current decoder automatically supplies gold context or that SURGE improves full-sentence CER.

CLI:

    python tools/evaluate_context_ablation.py --lexicon work/lexicon.tsv --corpus work/tokenized.tsv --train-sequences work/context/train_sequences.tsv --context-margin 0.5 --min-test-tokens 1000

## 4. CI and resource policy

The Sudachi Pack workflow downloads the pinned UD corpus, prepares train/dev/test, runs the contextual ablation, then builds and memory-checks the ordinary untrained reference Pack.

- Corpus and derived counts remain in the ephemeral GitHub Actions workspace.
- Only aggregate metrics and source hashes are printed to CI logs.
- No trained Pack, corpus file, or train sequence artifact is uploaded.
- Runtime Pack memory gate remains Peak RSS <= 1,792 MiB.
- The real-corpus minimum test coverage gate is 1,000 candidate-covered tokens; failure to meet it fails CI rather than silently presenting a tiny sample as representative.

## 5. Interpretation and limitations

- UD Japanese-GSD is a news/blog treebank. Its annotations are CC BY-SA 4.0; upstream license text separately cautions that the underlying source sentences may have independent copyright status. The project uses it as a transient benchmark and does not redistribute it.
- The adapter's pronunciation source is UniDicInfo pronunciation, not an independently human-verified kana transcription.
- Dictionary coverage filtering can bias the sample. The adapter reports retained and excluded tokens so the coverage denominator remains visible.
- Gold-neighbor teacher forcing overestimates context availability compared with a fully predicted decoding path. End-to-end CER and runtime SURGE integration remain separate future evaluations.
- No accuracy gain is asserted until CI reports real test-set measurements.

## 6. Compatibility

- No C++ scoring, public API, C ABI, Pack schema, or runtime memory guardrail changes.
- New tools are offline Python utilities and add no runtime dependencies.
