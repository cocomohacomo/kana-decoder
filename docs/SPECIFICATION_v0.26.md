# kana-decoder 仕様書 v0.26

- Version: 0.26.0
- Core license: MIT
- Runtime: CPU-only / offline
- Priority: functional correctness first; deterministic resource ceilings before footprint optimization.

## 1. Decoder resource contract

Every decode is subject to deterministic structural ceilings. Defaults:

| Resource | Default ceiling |
|---|---:|
| Input reading | 256 Unicode code points |
| Lattice edges | 4,096 |
| Retained lattice candidates | 32,768 |
| Expanded morphology states | 262,144 |
| Score trace records | 4,096 |
| Beam width / edges per position / candidates per edge / output paths | 8 / 16 / 8 / 8 |

- `DecoderOptions` exposes these limits so a caller can choose stricter budgets. Caller-supplied values are clamped to the hard ceilings and cannot raise them.
- The lattice builder bounds edge count and candidate copies while constructing the lattice. Its top-8 candidate retention uses stable bounded insertion, preserving deterministic tie order without copying the full dictionary value list. Candidate retention uses a bounded partial sort, avoiding a full copy of a high-ambiguity dictionary node.
- The morphology decoder stops before exceeding the expansion budget.
- Input, lattice, or expansion budget exhaustion fails closed with an empty result and sets the optional `resource_limit_exceeded` output flag. It never returns a silently truncated best path.
- Trace output is capped by both the caller limit and the hard 4,096-record ceiling.
- Streaming sessions retain their existing input cap and also inherit the decoder's per-call limits.
- The ceilings bound decoder scratch/work structurally. They do not claim to cap the memory occupied by the loaded Language Pack itself.

## 2. Process-level memory gate

- The Sudachi runtime workflow applies Linux `RLIMIT_DATA` of 3,072 MiB to leave room for allocator/data-segment overhead to the native evaluator process.
- GNU `time` records peak RSS, and `tools/check_memory_budget.py` fails the workflow if measured peak RSS exceeds 1,792 MiB.
- The RSS gate includes the loaded Pack and runtime allocations, unlike the decoder's structural scratch limits.
- The RLIMIT_DATA allocation ceiling and the measured RSS ceiling are intentionally separate: the former is 3,072 MiB, while the latter remains 1,792 MiB. The first 1,792 MiB data-segment attempt failed with std::bad_alloc during Pack load, so it was raised to avoid confusing virtual allocation pressure with resident-memory usage. This is a CI enforcement envelope, not a universal device guarantee. Actual target-device limits must be benchmarked on the target hardware and Pack.

## 3. Lightweight SURGE probe

- `tools/evaluate_surge.py` is a non-mutating diagnostic layer over evaluator trace JSON.
- It groups candidate surfaces by reading span into small claim clusters.
- Multiple incompatible surfaces in one span create an unresolved question; the least-separated candidate set receives focus.
- It reports candidate coverage against the gold surface, baseline top-1 accuracy, claim/question counts, and focus spans.
- The probe does not change decoder output, does not use gold labels to select a prediction, and does not auto-resolve a question. Gold is used only for offline coverage measurement.
- This is a lightweight SURGE-inspired evaluation probe, not the full SURGE framework or a semantic model.

## 4. Regression requirements

- C++ tests verify input, lattice-edge, and morphology-expansion budget exhaustion; trace cap; and fail-closed behavior.
- Python tests verify RSS-log parsing and SURGE claim/question/focus behavior.
- Sudachi Pack CI executes native evaluation under the process memory ceiling, checks measured RSS, then runs the SURGE probe on real trace output.
- Resource-limit changes must preserve results for inputs below all configured ceilings. Accuracy changes require separate held-out corpus evidence.

## 5. Known quality baseline and interpretation

The real Sudachi Small Pack run `44381438082` completed successfully:
- Peak RSS: 1,425,504 KiB (approximately 1,392 MiB).
- Four-case smoke corpus: 1/4 exact (25%), CER 1.0, decode failures 0.
- Outputs: `あめ` → `アメ` (gold `雨`); `はし` → `橋`; `きしゃ` → `記者` (gold `汽車`); `ぴ` → `日` (gold `ぴ`).
- Gold candidates were present in the retained candidate lattice for all four smoke cases (4/4 candidate coverage), while the baseline ranker selected the gold surface only once (1/4).
- All observed raw context scores were zero because the Pack contains no learned n-gram records. The probe can therefore preserve ambiguities and create questions, but has no evidence with which to reliably resolve the homophones.
- This four-case smoke set is diagnostic only and cannot estimate general Japanese conversion accuracy.

## 6. Compatibility

- Existing call sites remain source-compatible through default arguments.
- C ABI and Pack file format are unchanged.
- Existing beam/edge/candidate/path maxima remain in force; the new budgets add outer ceilings and fail-closed signaling.
- Warning-as-error policy remains mandatory.
