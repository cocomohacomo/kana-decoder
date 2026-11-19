#ifndef KANA_DECODER_C_API_H
#define KANA_DECODER_C_API_H
#include <stddef.h>
#include <stdint.h>
#define KD_C_API_VERSION 1u
#ifdef __cplusplus
extern "C" {
#endif
typedef struct kd_ngram_scorer kd_ngram_scorer;
typedef struct kd_engine kd_engine;
typedef struct kd_streaming_session kd_streaming_session;
typedef struct kd_decode_result kd_decode_result;
typedef struct kd_token_view {
    size_t start_codepoint;
    size_t end_codepoint;
    const char* candidate_id;
    const char* surface;
    const char* pos;
    const char* inflection_type;
    const char* inflection_form;
    double morphology_score;
    double context_score;
} kd_token_view;
typedef struct kd_candidate_view { const char* id; const char* surface; } kd_candidate_view;
enum { KD_OUTPUT_NORMAL=0,KD_OUTPUT_AMBIGUOUS=1,KD_OUTPUT_CONSERVATIVE=2,KD_OUTPUT_DIAGNOSTIC=3 };
enum { KD_ERR_INVALID_ARGUMENT=-1,KD_ERR_BUFFER_TOO_SMALL=-2,KD_ERR_INTERNAL=-3 };
uint32_t kd_c_api_version(void);
kd_ngram_scorer* kd_ngram_scorer_create(void);
kd_engine* kd_engine_create(void);
void kd_engine_destroy(kd_engine*);
// Override the default ephemeral journal with a caller-managed persistent SURGE journal.
int kd_engine_set_surge_journal(kd_engine*,const char* journal_path);
// Loads local lexicon.tsv, ngram.tsv and optional connections.tsv. Pack hashes should be verified offline first.
int kd_engine_load_pack(kd_engine*,const char* pack_directory);
// Returns best-path surface text. 0 success; negative values are KD_ERR_* codes.
int kd_engine_decode(kd_engine*,const char* reading,char* output,size_t output_capacity);
// N-best result owns all returned strings. Views remain valid until result destruction.
kd_decode_result* kd_engine_decode_nbest(const kd_engine*,const char* reading,size_t max_paths);
void kd_decode_result_destroy(kd_decode_result*);
size_t kd_decode_result_path_count(const kd_decode_result*);
int kd_decode_result_text(const kd_decode_result*,size_t path_index,char* output,size_t capacity);
int kd_decode_result_path_score(const kd_decode_result*,size_t path_index,double* score);
size_t kd_decode_result_token_count(const kd_decode_result*,size_t path_index);
int kd_decode_result_token(const kd_decode_result*,size_t path_index,size_t token_index,kd_token_view* view);
// A streaming session owns a snapshot of the loaded engine. It remains valid if the
// original engine is later destroyed or reloaded.
kd_streaming_session* kd_streaming_session_create(const kd_engine*,size_t max_reading_codepoints);
void kd_streaming_session_destroy(kd_streaming_session*);
int kd_streaming_append(kd_streaming_session*,const char* chunk,char* provisional,size_t capacity,uint64_t* revision);
int kd_streaming_commit_common_prefix(kd_streaming_session*,char* committed,size_t capacity,uint64_t* revision);
int kd_streaming_commit_best(kd_streaming_session*,char* committed,size_t capacity,uint64_t* revision);
int kd_streaming_reset(kd_streaming_session*);

void kd_ngram_scorer_destroy(kd_ngram_scorer*);
int kd_ngram_add_unigram(kd_ngram_scorer*,const char*,uint32_t);
int kd_ngram_add_left_bigram(kd_ngram_scorer*,const char*,const char*,uint32_t);
int kd_ngram_add_right_bigram(kd_ngram_scorer*,const char*,const char*,uint32_t);
// 0 success, -1 invalid arguments, -2 output buffer too small.
int kd_ngram_resolve(kd_ngram_scorer*,const kd_candidate_view*,size_t,const char* const*,size_t,const char* const*,size_t,int,double,double,int,const char*,char*,size_t);
#ifdef __cplusplus
}
#endif
#endif
