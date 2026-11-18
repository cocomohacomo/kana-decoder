# kana-decoder 仕様書 v0.11

- Status: Implementation milestone specification
- Version: 0.11.0
- Core license: MIT
- Runtime: CPU only / fully offline

## 1. Scope
v0.11 exposes the existing C++ StreamingSession through a stable C ABI so C and other FFI consumers can append kana chunks, inspect provisional best-path output, and explicitly commit either a common N-best prefix or the current best path.

## 2. Session ownership
A C streaming session is created from a loaded kd_engine and owns copies of the lexicon, scorer and connection model. It does not borrow engine storage. The original engine may be destroyed or reloaded after session creation without invalidating the session. A session is single-caller mutable state; callers must externally synchronize concurrent access to the same handle.

## 3. API
- kd_streaming_session_create(engine, max_reading_codepoints): create a session; zero selects the default 256 code-point window.
- kd_streaming_session_destroy(session): release session state.
- kd_streaming_append(session, chunk, provisional, capacity, revision): append UTF-8 kana and return the current best-path surface as provisional text.
- kd_streaming_commit_common_prefix(session, output, capacity, revision): commit only the token prefix identical across all retained N-best paths.
- kd_streaming_commit_best(session, output, capacity, revision): explicitly commit the best retained path.
- kd_streaming_reset(session): clear pending input while advancing internal revision.

Return values use the existing KD_ERR_* family: 0 success, -1 invalid state/input or unavailable commit, -2 output buffer too small, -3 internal exception.

## 4. Transactional output behavior
Append and commit operations execute against a temporary copy of session state. State is published only after the complete output fits in the caller's buffer. A buffer-too-small result MUST NOT consume input, advance revision, or commit tokens. Revision output is written only on success.

## 5. Uncertainty and commit safety
The provisional string is the highest-ranked retained path, not a final commitment. Common-prefix commit requires exact equality of token span, candidate ID and surface across all retained paths. If no common token exists, it returns KD_ERR_INVALID_ARGUMENT and leaves state unchanged. Best-path commit is an explicit caller decision and may discard ambiguity.

The common-prefix guarantee is bounded by the retained N-best beam and does not imply that pruned paths were considered.

## 6. Validation
- C source must compile and link against the exported C API.
- Streaming append must return a provisional result and monotonic revision.
- Best-path commit must return the selected surface and clear pending input.
- Repeated commit on an empty session must fail without changing state.
- Buffer-too-small append must be transactional.
- Session-owned Pack state must remain usable after the source engine is destroyed.
- Existing C++ engine, Pack, dictionary importer, license and corpus tests remain passing.

## 7. Remaining work
- Expose structured N-best candidate snapshots and token spans in a versioned C ABI.
- Add C ABI common-prefix success tests using deliberately ambiguous prefixes.
- Add a Python ctypes integration test and package ABI compatibility checks.
- Add real dictionary and held-out corpus quality evaluation.
- Measure streaming append/commit latency and peak RSS on target CPU hardware.
