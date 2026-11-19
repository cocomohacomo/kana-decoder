# kana-decoder: Provider-neutral streaming boundary v0.48

Version: 0.48.0
Parent: SPECIFICATION_v0.47.md

## 1. Objective

Remove Japanese decoder implementation dependencies from the generic streaming state machine while preserving the public C API, Python wrapper behavior and SURGE UnifiedDecision integration.

## 2. Contract

- `StreamingSession` owns only bounded input reading state, revision and commit state.
- A Language Pack owns decoding and supplies complete `DecodedPath` results to the generic session.
- The generic session must not include or accept Japanese `LexiconTrie`, `MaskedNgramScorer` or `ConnectionModel` arguments.
- The session validates UTF-8, non-empty chunks and both session/configured input ceilings before committing state.
- A rejected append must leave the accepted reading, snapshot and revision unchanged.
- The Pack remains responsible for decoding the full accumulated reading, so chunk boundaries do not change the decoder's view of context.

## 3. Implementation

- Replaced the Japanese-resource-coupled `StreamingSession::append` with `append_decoded`.
- Updated the Japanese Language Pack provider to decode the accumulated reading and pass paths to the generic session.
- Added a transactional over-limit append regression to `tests/test_language_pack.cpp`.
- The C API remains Pack-mediated and requires no Japanese resource knowledge.

## 4. Remaining separation

The generic public `engine.hpp` still declares Japanese implementation-oriented types and functions, including lattice/trie/morphology and resource-loader interfaces. Moving these into a Language Set-private or provider-facing header is still required to complete host/provider decoupling. Existing evaluation tools and legacy compatibility tests also use those declarations and must be migrated deliberately.

## 5. Verification

All static/shared builds, CTest targets, C API tests, Python integration tests, golden-output parity and target-device resource measurements remain required. No build success or quality/performance improvement is claimed until CI and benchmarks confirm them.

## 6. Scope

Only `cocomohacomo/kana-decoder`, branch `refactor/surge-core-as-runtime`, is modified. The separate SURGE repository is untouched.
