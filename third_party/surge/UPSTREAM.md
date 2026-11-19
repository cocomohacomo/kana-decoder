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


## Decision API integration delta

The following new files are sourced from SURGE PR #139, branch
`feat/language-runtime-core`, head commit
`7e8ba7c80df8bcc4c487ccf14d7b99abbe1200b3`:

- `include/surge/decision.hpp`
- `src/decision.cpp`

They provide the Core-owned RecognitionDecision and NormalizationDecision
contracts, deterministic bounded candidate comparison, family-weight calibration,
fail-closed handling of non-finite contributions and accumulated scores, and a hard cap on family-weight entries. These files are
vendored before PR #139 is merged; once upstream is merged, update the pinned
commit and remove this temporary source-delta note after verifying byte parity.
The local adapter integration in `src/surge_runtime.cpp` remains kana-decoder
specific and is not part of upstream SURGE Core.
