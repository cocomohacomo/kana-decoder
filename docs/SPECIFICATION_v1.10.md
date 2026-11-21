# kana-decoder Specification v1.10: Held-out evaluation and warning cleanup

Parent: SPECIFICATION_v1.09.md

## 1. Scope

This revision records the completed pinned Sudachi Small + UD Japanese-GSD evaluation at PR head `baccef9a2f08486f8ad2ef5e1e74e270aa497956`, fixes a compiler warning in the SURGE runtime regression test, and defines the evidence-based next work.

The evaluation is diagnostic. The selected policy is not promoted to a production default because held-out SURGE Core did not outperform Decoder Top-1.

## 2. Evaluation provenance

- Workflow: Build Sudachi Small, run `44879683317`
- Workflow result: success
- Evaluated PR head: `baccef9a2f08486f8ad2ef5e1e74e270aa497956`
- Corpus: pinned UD Japanese-GSD, 2,414 held-out test sentences
- Gold tokens: 10,317
- Policy calibration: development split only
- Selection rule: minimum dev CER, then maximum dev sentence exact, then fixed preset order
- Selected preset: `margin_guard_025`
- Selected minimum recognition margin: 0.25
- Held-out test was evaluated once with the selected policy; no test-driven tuning was performed.

The selected development policy measured dev CER 0.496759 and sentence exact 0.257422. These development metrics are not held-out performance claims.

## 3. Held-out candidate generation

| Metric | Test result |
|---|---:|
| Mean N-best paths | 7.750621 |
| Sentence raw-surface recall@1 | 0.239437 |
| Sentence raw-surface recall@5 | 0.461889 |
| Sentence raw-surface recall@10 | 0.504143 |
| Oracle CER@5 | 0.343325 |
| Oracle CER@10 | 0.307512 |
| Token-aligned recall@1 | 0.539207 |
| Token-aligned recall@5 | 0.680333 |
| Token-aligned recall@10 | 0.714452 |

At token top-10, the miss partition was:

- Exact span and surface: 7,371
- Span matched, surface mismatched: 1,804
- Surface matched, span mismatched: 69
- No matching surface candidate: 1,073

The largest single error class is same-span surface mismatch. Candidate-generation work should therefore prioritize lexical/surface alternatives and inflection realization before simply increasing span search. The no-surface-candidate group remains a separate coverage problem.

Lattice diagnostics reported 40,769/40,769 span matches retained and 257,315 of 797,147 candidate records retained. The large aggregate pruned count alone does not establish harmful pruning: only gold-aligned miss attribution can identify which retained candidate cap or stage needs adjustment. Any future change must preserve hard memory and candidate bounds.

## 4. Held-out decision results

| Metric | Decoder Top-1 | SURGE Core |
|---|---:|---:|
| CER | 0.508126 | 0.518117 |
| Sentence exact | 0.239437 | 0.236123 |
| Runtime failures | 0 | 0 |
| Decode failures | 0 | 0 |

SURGE Core did not improve the held-out test result. Its CER was 0.009991 worse and sentence exact was 0.003314 lower than Decoder Top-1. Candidate-available cases were partitioned into 535 deferred, 364 exact SURGE selections, and 318 wrong SURGE selections; there were no runtime failures. Missing-candidate cases numbered 1,197.

No accuracy improvement or production-default promotion is claimed. Keep the policy experimental and preserve Decoder Top-1 fallback.

## 5. Runtime and memory

- SURGE Core elapsed time: 113.77 seconds
- Throughput: 21.22 sentences/second
- Time per 1,000 input codepoints: 5.697 seconds
- Peak RSS: 1,225,896 KiB (about 1,197.2 MiB)
- Enforced process ceiling: 1,792 MiB

The hosted CI measurement passes the configured ceiling. It does not establish latency or memory on the target business PC.

## 6. Implementation cleanup

The partial-span typed-entity regression test contained a multi-character character literal for its newline diagnostic. It is corrected to the single-character newline literal. This removes the `-Wmultichar` compiler warning without changing test behavior or runtime code.

## 7. Remaining work and acceptance gates

1. Use development-split diagnostics to identify which candidate generation stage loses same-span surface alternatives; add a minimal reproducible regression fixture before changing retention or scoring.
2. Investigate the 1,073 no-surface-candidate token misses separately from the 1,804 same-span surface mismatches.
3. Reassess SURGE override quality using dev-only calibration and explicit confidence partitions. Do not tune against the held-out test result.
4. Preserve the current candidate, lattice, trace, expanded-state, and process RSS ceilings.
5. Re-run full C++/Python/Pack CI and the pinned real-data workflow after each substantive decoder change.
6. Keep PR #2 Draft and unmerged until held-out evaluation demonstrates a repeatable benefit and the user explicitly authorizes merge.

UD Japanese-GSD is a morphology/context benchmark, not a conversational corpus. Streaming conversational quality, target-device latency, package size, and clean offline installation remain separate product gates.

## 8. Safety boundaries

- No raw corpus sentence traces are committed or uploaded.
- Do not modify the separate SURGE main repository or vendored `third_party/surge`.
- Do not use held-out test metrics to select thresholds, weights, or candidate budgets.
- No unbounded candidate retention, persistent graph cache, or memory-ceiling relaxation is permitted.
