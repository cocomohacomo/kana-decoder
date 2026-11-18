# C API

## Engine and Pack loading

The C ABI is declared in include/kana_decoder/c_api.h. Build the shared/static library with CMake and link against kana_decoder_core.

    kd_engine *engine = kd_engine_create();
    if (!engine) return -1;
    if (kd_engine_load_pack(engine, "./packs/ja-lite") != 0) {
        kd_engine_destroy(engine);
        return -1;
    }

The engine loads local Pack data only. No network access or automatic Pack download occurs.

## One-shot conversion

    char output[1024];
    int rc = kd_engine_decode(engine, "きょうははれる", output, sizeof(output));

A successful call returns zero and writes a NUL-terminated UTF-8 surface string. Negative return values use KD_ERR_INVALID_ARGUMENT, KD_ERR_BUFFER_TOO_SMALL or KD_ERR_INTERNAL.

## Streaming conversion

A streaming session takes an independent snapshot of the engine's loaded lexicon, scorer and connection model. It remains valid if the original engine is destroyed or reloaded.

    kd_streaming_session *session = kd_streaming_session_create(engine, 256);
    char provisional[1024];
    uint64_t revision = 0;

    kd_streaming_append(session, "きょうは", provisional, sizeof(provisional), &revision);
    kd_streaming_append(session, "はれる", provisional, sizeof(provisional), &revision);

The returned string is the best currently retained path and remains provisional. It may change as more kana arrives.

Commit explicitly when the caller has enough context:

    char committed[1024];
    kd_streaming_commit_common_prefix(session, committed, sizeof(committed), &revision);

Common-prefix commit accepts only the exact token prefix shared by every retained N-best path. If no such token exists, the call returns KD_ERR_INVALID_ARGUMENT and leaves the session unchanged. kd_streaming_commit_best explicitly commits the highest-ranked path and may discard ambiguity.

kd_streaming_reset clears pending input. Destroy the session with kd_streaming_session_destroy.

## Transactional buffer behavior

Append and commit calls stage changes in a temporary session copy. If the output buffer is too small, the function returns KD_ERR_BUFFER_TOO_SMALL and does not consume input, commit tokens or advance the revision output parameter. Allocate enough space and retry the same operation.

## Return values

| Code | Meaning |
|---|---|
| 0 | Success |
| KD_ERR_INVALID_ARGUMENT (-1) | Invalid handle/input/state, or no safe common-prefix commit |
| KD_ERR_BUFFER_TOO_SMALL (-2) | Output buffer cannot hold UTF-8 result and NUL terminator |
| KD_ERR_INTERNAL (-3) | Unexpected internal exception |

A single session handle is mutable and must not be used concurrently without caller-side synchronization. The session owns a copy of Pack runtime data; callers must still destroy it explicitly.
