#ifndef KANA_DECODER_C_API_H
#define KANA_DECODER_C_API_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct kd_ngram_scorer kd_ngram_scorer;
typedef struct kd_engine kd_engine;
typedef struct kd_candidate_view { const char* id; const char* surface; } kd_candidate_view;
enum { KD_OUTPUT_NORMAL=0,KD_OUTPUT_AMBIGUOUS=1,KD_OUTPUT_CONSERVATIVE=2,KD_OUTPUT_DIAGNOSTIC=3 };
enum { KD_ERR_INVALID_ARGUMENT=-1,KD_ERR_BUFFER_TOO_SMALL=-2,KD_ERR_INTERNAL=-3 };
kd_ngram_scorer* kd_ngram_scorer_create(void);
kd_engine* kd_engine_create(void);
void kd_engine_destroy(kd_engine*);
// Loads local lexicon.tsv, ngram.tsv and optional connections.tsv. Pack hashes should be verified offline first.
int kd_engine_load_pack(kd_engine*,const char* pack_directory);
// Returns best-path surface text. 0 success; negative values are KD_ERR_* codes.
int kd_engine_decode(kd_engine*,const char* reading,char* output,size_t output_capacity);
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
