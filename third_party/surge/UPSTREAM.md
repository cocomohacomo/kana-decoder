# Vendored SURGE Core

- Upstream: https://github.com/cocomohacomo/surge
- Pinned upstream commit: c8a0b5a5f8cf714b74892d1263bd53088fe51be3
- Specification: SURGE v1.0 (Core implementation version 0.9.11)
- License: MIT (see LICENSE)

This directory vendors the production Core headers and implementation sources so
kana-decoder builds and runs fully offline. It is not a forked reimplementation.
Changes to this subtree must be reviewed against the pinned upstream revision.

## Local integration delta

The vendored `src/context_graph.cpp` adds replay accounting for the four
kana-decoder adapter event kinds `NORMALIZATION_COMMIT`,
`NORMALIZATION_PROPOSAL`, `NORMALIZATION_STREAM_PREFIX_COMMIT` and
`NORMALIZATION_STREAM_FINAL_COMMIT`. Each is one EventJournal transaction and
must advance ContextGraph's recovered shared state version exactly once. All
other vendored Core source files and headers are copied from the pinned upstream
commit. Do not update the pin without revalidating journal replay conformance.
