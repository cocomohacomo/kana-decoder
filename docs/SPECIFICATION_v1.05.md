# kana-decoder Specification v1.05: Repeatable SURGE runtime benchmark

Parent: SPECIFICATION_v1.04.md

## Scope

This revision improves the observability of SURGE runtime latency. It does not change decoder scoring, beam pruning, candidate limits, SURGE decisions, or production runtime behavior.

## Benchmark protocol

- The benchmark accepts an operation count and repeat count. The default repeat count is five and is clamped to at least three.
- Each measured repeat creates a fresh runtime instance and a unique journal/context identifier.
- Each repeat performs a warm-up phase before timing.
- The single-candidate and ambiguous two-candidate cases are measured independently.
- The report emits median, minimum, and maximum nanoseconds per normalization, plus median total nanoseconds.
- The reported graph-node and journal-version counters are from the final repeat and are diagnostic, not timing metrics.

## Interpretation

- The median is the primary comparison value; minimum and maximum expose run-to-run spread.
- Comparisons must use the same runner class, build configuration, operation count, warm-up policy, and repeat count.
- This microbenchmark does not measure full Japanese Pack decoding, corpus accuracy, or target-device latency.
- The benchmark intentionally creates a fresh runtime for each repeat so journal growth from one measured sample cannot contaminate the next sample.

## CI

The regular C++ workflow runs 50 measured normalizations per repeat and five repeats for both modes. CI success validates execution and output production, not a performance improvement. A performance claim requires comparing the emitted repeated-sample statistics against a same-environment baseline.
