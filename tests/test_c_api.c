#include "kana_decoder/c_api.h"
#include <assert.h>
#include <string.h>
int main(void) {
    kd_engine *engine = kd_engine_create();
    char output[64] = {0};
    assert(engine != NULL);
    assert(kd_engine_load_pack(engine, KANA_DECODER_PACK_DIR) == 0);
    assert(kd_engine_decode(engine, "あめ", output, sizeof(output)) == 0);
    assert(strcmp(output, "雨") == 0);
    assert(kd_engine_decode(engine, "あめ", output, 1) == KD_ERR_BUFFER_TOO_SMALL);
    kd_streaming_session *session = kd_streaming_session_create(engine, 16);
    uint64_t revision = 0;
    char provisional[64] = {0};
    char committed[64] = {0};
    assert(session != NULL);
    assert(kd_streaming_append(session, "あ", provisional, sizeof(provisional), &revision) == 0);
    assert(revision == 1 && strcmp(provisional, "あ") == 0);
    kd_streaming_session *buffer_session = kd_streaming_session_create(engine, 16);
    assert(buffer_session != NULL);
    char tiny[1] = {0};
    uint64_t untouched_revision = 99;
    assert(kd_streaming_append(buffer_session, "あ", tiny, sizeof(tiny), &untouched_revision) == KD_ERR_BUFFER_TOO_SMALL);
    assert(untouched_revision == 99);
    kd_engine_destroy(engine); /* sessions own their Pack snapshot */
    assert(kd_streaming_append(buffer_session, "め", provisional, sizeof(provisional), &revision) == 0);
    assert(revision == 1 && strcmp(provisional, "め") == 0);
    kd_streaming_session_destroy(buffer_session);
    assert(kd_streaming_append(session, "め", provisional, sizeof(provisional), &revision) == 0);
    assert(revision == 2 && strcmp(provisional, "雨") == 0);
    assert(kd_streaming_commit_common_prefix(session, committed, sizeof(committed), &revision) == KD_ERR_INVALID_ARGUMENT);
    assert(kd_streaming_commit_best(session, committed, sizeof(committed), &revision) == 0);
    assert(revision == 3 && strcmp(committed, "雨") == 0);
    assert(kd_streaming_commit_best(session, committed, sizeof(committed), &revision) == KD_ERR_INVALID_ARGUMENT);
    assert(kd_streaming_reset(session) == 0);
    kd_streaming_session_destroy(session);
    return 0;
}
