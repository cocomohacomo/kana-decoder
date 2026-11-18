# kana-decoder 仕様書 v0.28

- Version: 0.28.0
- Core license: MIT
- Runtime: CPU-only / offline
- Priority: contextual correctness, evidence provenance, leakage-safe corpus evaluation.

## 1. Context corpus compiler

Add tools/prepare_context_corpus.py to convert externally tokenized sentence data into Pack training and held-out evaluation inputs.

Input TSV header:

    document_id<TAB>sentence_id<TAB>reading<TAB>surface<TAB>domain[<TAB>split]

- Each row is one token in sentence order.
- The normalized lexicon is matched by exact (reading, surface) pair; candidate ID is resolved deterministically by lowest word cost then candidate ID.
- Missing dictionary entries fail closed with source line number. The compiler does not silently drop tokens or invent candidate IDs.
- If an explicit split column is supplied, only train/dev/test are accepted and a document may not appear in multiple splits.
- Without explicit split values, documents are deterministically ordered by SHA-256 and assigned to train/dev/test as close to 80/10/10 as possible. One- and two-document inputs use the available splits without fabricating groups.
- All sentences from a document remain in one split.
- Train output is one candidate-ID sequence per sentence for build_language_pack.py --sequences.
- Dev/test outputs are reading, expected surface, and domain TSV rows. They are not included in training counts.
- A corpus manifest records document/sentence/token counts per split.

CLI:

    python tools/prepare_context_corpus.py --lexicon normalized-lexicon.tsv --corpus tokenized-corpus.tsv --output-dir work/context-data

The compiler expects tokenized input. It does not perform morphological tokenization itself; a language-specific tokenizer/adapter must create the token TSV. Corpus licensing and redistribution permissions remain the caller's responsibility.

## 2. Candidate ablation evaluator

Add tools/evaluate_ablation.py to analyze evaluator trace JSON without modifying decoder output.

- Morphology-only top-1: highest morphology score per candidate surface.
- Combined-local top-1: highest local score per candidate surface.
- SURGE-gated resolution: requires at least two non-zero differentiated context evidence scores and a best-vs-second margin at or above the configured threshold. Otherwise it abstains.
- For each surface, score components are taken from the same best-local observation to avoid mixing incompatible beam histories.
- Gold labels are used only for offline candidate coverage and accuracy metrics.
- To avoid guessing token-to-character alignment, this evaluator only scores spans covering the complete reading. Multi-span cases are reported as ineligible, not scored against sentence-level gold.
- Reports candidate coverage, morphology and combined-local top-1 accuracy, SURGE resolution count, precision and recall over covered cases, plus the original end-to-end decoder metrics.
- This is an offline diagnostic, not a new runtime decoder or a claim that SURGE improves end-to-end conversion.

CLI:

    python tools/evaluate_ablation.py evaluation.json --context-margin 0.5

## 3. Existing Pack training integration

The Pack builder already accepts one token candidate-ID sequence per line using --sequences, producing unigram (U), left-context (L), and right-context (R) count records. The v0.28 compiler supplies a deterministic and leakage-safe source for that interface. No third-party training corpus is bundled.

## 4. Validation and current evidence

- Corpus compiler tests verify document-disjoint splitting, explicit split conflict rejection, and strict unknown-token rejection.
- Ablation tests verify context evidence can resolve a synthetic ambiguity when morphology prefers the wrong candidate, no-context cases abstain, and multi-span cases are not misaligned.
- Real Sudachi Pack workflow v0.27, run 44388413830, completed successfully:
  - Pack entries: 754,602.
  - Trained n-gram records: 0.
  - Installed Pack size: 661.413 MiB.
  - Pack load: 10,302 ms.
  - Peak RSS: 1,425,444 KiB, below the 1,792 MiB gate.
  - Four smoke cases: 1/4 exact, CER 1.0, decode failures 0.
  - SURGE: 13 span clusters, 84 claims, 12 Questions, 0 context-evidence spans, 0 resolved Questions, 12 unresolved Questions, 4 Focus cases, candidate coverage 4/4.
- The current smoke corpus is diagnostic only. No production accuracy claim is made.
- The repository does not yet contain an external tokenized train/dev/test corpus. The next measurable accuracy result depends on a licensed corpus with tokenizer-produced token boundaries.

## 5. Compatibility

- No C++ runtime scoring formula, public API, C ABI, or Pack format changes in v0.28.
- Existing Pack builder --sequences format remains unchanged.
- New tools are offline Python utilities and add no runtime dependencies.
