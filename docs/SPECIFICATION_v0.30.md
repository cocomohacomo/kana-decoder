# kana-decoder 仕様書 v0.30

- Version: 0.30.0
- Core license: MIT
- Runtime: CPU-only / offline
- Focus: causal sequential context evaluation and explicit oracle-vs-causal separation.

## 1. Evaluation background

v0.29 introduced a held-out token-level ablation using UD Japanese-GSD. Its initial evaluator supplied gold left and right neighboring candidate IDs. This is useful for isolating context scoring, but it is an oracle-context upper bound and can overstate what a streaming decoder can know.

The pinned corpus is unchanged:
- UD Japanese-GSD commit: `c662dc96ed5ee5ac0fc8b5e44261557e431f50e6`
- Official train/dev/test splits; document-disjoint source split.
- Corpus and derived counts are fetched/generated in ephemeral CI workspace only. They are not committed or uploaded.

## 2. Two evaluation tracks

`tools/evaluate_context_ablation.py` now reports two distinct tracks.

### 2.1 Oracle bidirectional ablation

- Uses gold previous and next candidate IDs.
- Compares morphology-only, morphology + unigram + left/right context, and context-evidence-gated SURGE.
- This remains a scoring-isolation upper bound, not a production prediction result.
- Candidate count is capped at eight and same-surface morphological variants are collapsed by best score.
- The SURGE gate requires at least two distinct candidate surfaces with observed contextual support and a score margin >= configured threshold. Otherwise it abstains.

### 2.2 Causal sequential evaluation

- Uses gold token boundaries and readings only to isolate candidate scoring. It does not claim to evaluate tokenization or full reading-to-sentence decoding.
- Each policy maintains its own predicted candidate-ID history. No gold neighboring candidate IDs or future context are supplied.
- The morphology policy chooses by negative dictionary word cost and candidate-local connection cost.
- The combined policy adds unigram prior and left-context bigram evidence to morphology score.
- The SURGE-gated policy resolves only when at least two distinct candidate surfaces have non-zero left-context evidence and their best-vs-second margin meets the configured threshold. Otherwise it falls back to the morphology choice.
- Each policy updates its own history with its selected candidate. This prevents one policy from borrowing another policy's predictions.
- Reports candidate coverage, top-1 accuracy, candidate misses, and SURGE resolution precision/recall/abstention.
- The evaluator processes the test sequence in order. Right-context counts may exist in the training statistics but are not read by the causal scoring path.

The causal track is a teacher-segmented sequential diagnostic. A full end-to-end evaluation still requires the decoder to predict segmentation and candidate paths without gold token boundaries.

## 3. Scoring and resource boundaries

- Additive smoothing remains alpha=0.5, matching `MaskedNgramScorer`.
- Token-local morphology scoring includes dictionary word cost and candidate-local connection cost. Full connection-matrix transition costs are outside this ablation.
- Candidate generation is approximated by dictionary entries matching the gold reading, sorted deterministically and capped at eight.
- The SURGE gate uses context evidence only. Morphology cost and unigram prior cannot independently resolve a SURGE question.
- Evaluation counts are computed from train split only. Test labels are used only for scoring and coverage.
- Corpus files and counts remain transient CI data; no trained Pack or derived model is distributed.

## 4. v0.29 baseline measurement

GitHub Actions run `44394039136` completed successfully:
- Test source tokens: 10,317; candidate-covered tokens: 9,774 (94.74%).
- Morphology-only top-1 accuracy: 45.2936%.
- Combined unigram + bidirectional context top-1 accuracy: 45.4164% (+0.1228 percentage points).
- Oracle SURGE resolved 875 tokens, abstained on 8,899; resolution precision 90.8571%, correct-resolution recall over covered tokens 8.1338%.
- These are oracle-context measurements. They are not causal or end-to-end accuracy claims.\n\n## 4.1 v0.30 causal measurement\n\nGitHub Actions run `44405759595` completed successfully using the same pinned corpus and dictionary:\n- Candidate coverage: 9,774 / 10,317 (94.74%).\n- Causal morphology-only: 45.2936% (4,427 correct tokens).\n- Causal morphology + unigram + predicted-left context: 45.3857% (4,436 correct, +0.0921 percentage points).\n- Causal SURGE-gated with morphology fallback: 46.2861% (4,524 correct, +0.9925 percentage points versus morphology-only).\n- SURGE resolved 151 tokens and abstained on 10,166; resolution precision 85.4305%, correct-resolution recall over covered tokens 1.3198%.\n- This is a token-aligned causal diagnostic, not full end-to-end conversion accuracy. Gold token boundaries/readings remain supplied. The SURGE overall accuracy includes morphology fallback on abstentions, so the gain is a selective reranking effect, not 46.29% SURGE-only coverage.

## 5. CI and runtime guardrails

- The real Sudachi Pack workflow runs corpus preparation and both evaluation tracks before the native Pack smoke test.
- The runtime Pack Peak RSS gate remains <= 1,792 MiB on the GitHub-hosted runner.
- Decoder hard ceilings remain enforced in the C++ Core: input length, lattice edges, candidates, expanded states, beam width, and trace records.
- This evaluation refactor does not change C++ scoring, public APIs, C ABI, Pack schema, or runtime memory ceilings.

## 6. Interpretation

- The small oracle gain indicates that unigram + adjacent context, as currently represented, contributes little aggregate top-1 improvement on this filtered news/blog test set.
- High oracle SURGE precision with low resolution recall is a selective-coverage tradeoff, not broad accuracy improvement.
- The causal sequential results must be read separately. If they degrade, investigate error propagation in predicted history before changing runtime scoring weights.
- UD Japanese-GSD is a news/blog benchmark; it is not representative of all Japanese domains or conversational input.
