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
    assert(kd_engine_decode(engine, "あめ", output, 1) == -2);
    kd_engine_destroy(engine);
    return 0;
}
