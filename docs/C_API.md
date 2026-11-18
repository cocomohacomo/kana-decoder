# C API

## Engine and Pack loading

The C ABI is declared in \`include/kana_decoder/c_api.h\`. Build the shared/static library with CMake and link against \`kana_decoder_core\` or load the shared \`kana_decoder\` library with an FFI.

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

A successful call returns zero and writes a NUL-terminated UTF-8 surface string.

## Structured N-best conversion

    kd_decode_result *result = kd_engine_decode_nbest(engine, "あめ", 8);
    if (!result) { /* invalid input, unloaded engine, or internal failure */ }
    size_t paths = kd_decode_result_path_count(result);
    for (size_t p = 0; p < paths; ++p) {
        double score = 0.0;
        kd_decode_result_path_score(result, p, &score);
        size_t tokens = kd_decode_result_token_count(result, p);
        for (size_t t = 0; t < tokens; ++t) {
            kd_token_view view;
            kd_decode_result_token(result, p, t, &view);
            /* view strings are borrowed from result */
        }
    }
    kd_decode_result_destroy(result);

Each token exposes code-point offsets in the original reading, candidate ID, surface, POS, inflection type/form, morphology score and context score. The total path score ranks alternatives. Scores are not calibrated probabilities.

\`kd_token_view\` string pointers remain valid only until \`kd_decode_result_destroy\`. Do not free them or use them after result destruction. \`max_paths=0\` selects 8; values above 8 are rejected.

## Streaming conversion

A streaming session owns an independent snapshot of the engine's loaded lexicon, scorer and connection model. It remains valid if the original engine is destroyed or reloaded.

    kd_streaming_session *session = kd_streaming_session_create(engine, 256);
    char provisional[1024];
    uint64_t revision = 0;
    kd_streaming_append(session, "きょうは", provisional, sizeof(provisional), &revision);

The returned string is the best currently retained path and remains provisional. It may change as more kana arrives.

\`kd_streaming_commit_common_prefix\` commits only the exact token prefix shared by all retained N-best paths. \`kd_streaming_commit_best\` explicitly commits the highest-ranked path and may discard ambiguity. \`kd_streaming_reset\` clears pending input.

## Python ctypes

CMake builds \`kana_decoder\` as a shared library. \`tests/test_ctypes.py\` demonstrates loading the exact shared-library path, loading a local Pack, and retrieving structured N-best token metadata from Python.

## Transactional buffer behavior

Append and commit calls stage changes in a temporary session copy. If the output buffer is too small, the function returns \`KD_ERR_BUFFER_TOO_SMALL\` and does not consume input, commit tokens or advance the revision output parameter.

## Return values

| Code | Meaning |
|---|---|
| 0 | Success |
| KD_ERR_INVALID_ARGUMENT (-1) | Invalid handle/input/state or unavailable common-prefix commit |
| KD_ERR_BUFFER_TOO_SMALL (-2) | Output buffer cannot hold UTF-8 result and NUL terminator |
| KD_ERR_INTERNAL (-3) | Unexpected internal exception |
