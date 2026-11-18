# kana-decoder 仕様書 v0.31

- Version: 0.31.0
- Core license: MIT
- Runtime: CPU-only / offline
- Focus: native C++ SURGE evidence-gated left-context scoring and comparable end-to-end evaluation.

## 1. Purpose

v0.30 demonstrated a modest gain from train-derived unigram and predicted-left context in the native C++ full-path evaluator. The separate teacher-segmented causal Python diagnostic showed a larger selective-gating gain, but that result did not exercise native segmentation and path search. v0.31 brings the conservative evidence gate into the production C++ decoder path so its effect can be measured without gold token boundaries.

## 2. Native SURGE gate

DecoderOptions adds:
- surge_left_context_gate, disabled by default.
- surge_context_margin, default 0.5.

When enabled, the decoder evaluates each active beam state and lattice edge:
1. Inspect the bounded candidate shortlist (maximum eight candidates).
2. Retain only candidates with an observed left-context count in the scorer.
3. Collapse morphological variants by distinct surface, retaining the best context-only score for each surface.
4. Require at least two distinct supported surfaces.
5. Require the best-vs-second context-only score margin to meet the configured threshold.
6. If the gate resolves, apply context-only scores to supported candidates. Unsupported candidates receive zero context score.
7. If the gate does not resolve, all candidates receive zero context score and morphology plus connection costs decide the edge.

The gate does not use unigram priors to establish evidence or resolve a question. It does not read right/future context. Existing default decoding behavior is unchanged because the option is disabled unless explicitly selected.

## 3. Evaluation tracks

The native sequential corpus evaluator now reports three independent full-path runs:
- morphology_only: empty scorer, no contextual evidence.
- trained_unigram_left_context: train-derived unigram and predicted-left context.
- surge_evidence_gated_left_context: train-derived scorer with the native evidence gate enabled; gate scoring is left-context-only.

Each run decodes the full concatenated sentence reading without gold token boundaries. Gold token segmentation is used only for post-decode token alignment. Sentence exact match and Unicode codepoint CER are computed against the full gold sentence surface. The test split is never used to construct training counts.

## 4. Metrics and interpretation

Each mode reports:
- sentence count, scored count, exact matches and exact accuracy;
- edit distance, reference codepoints and CER;
- decode failures and over-limit sentences;
- gold/predicted token counts, exact token alignment, alignment precision and recall.

The C++ evaluator emits a single valid JSON object with the three modes as named nested objects. The contract test parses this output and checks the three result sections.

The v0.30 reference full-path baseline remains:
- 2,414 test sentences / 10,317 gold tokens.
- Morphology-only: exact 24.6479%, CER 48.6578%.
- Unigram + predicted-left context: exact 24.8136%, CER 48.4772%.
- Peak RSS 1,425,568 KiB (1,392.2 MiB), elapsed 19.18 seconds on the GitHub-hosted runner.
These figures are historical baseline measurements, not v0.31 SURGE results.

## 5. Resource and compatibility constraints

- Candidate shortlist <=8, beam width <=8, existing expansion ceiling <=262,144.
- Gate evaluation performs at most one bounded pass over the candidate shortlist per active state and edge.
- No new model files, Pack schema changes, C ABI changes, or external runtime dependencies.
- The gate is opt-in; default decoder scoring remains backward compatible.
- Corpus data and train-derived counts remain ephemeral CI inputs and are not committed or packaged.

## 6. Validation status

The v0.31 implementation includes unit coverage for observed evidence, missing evidence abstention, distinct-surface support, and native evaluator JSON contract. CI run 44414980579 passed (CTest 21/21; Python 63 tests, 58 passed / 5 skipped). Sudachi Pack run 44414667242 passed with 2,414 sentences / 10,317 tokens. The default SURGE gate produced the same full-path result as morphology-only: exact 24.6479%, CER 48.6578%; ordinary unigram + predicted-left context achieved exact 24.8136%, CER 48.4772%. Peak RSS was 1,425,512 KiB (1,392.1 MiB), below the 1,792 MiB gate. The measured gate did not resolve a path often enough to change aggregate output, so no v0.31 SURGE accuracy gain is claimed. This result motivates dev-only calibration in v0.32.
