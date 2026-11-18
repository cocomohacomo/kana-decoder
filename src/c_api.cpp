#include "kana_decoder/c_api.h"
#include "kana_decoder/engine.hpp"
#include <algorithm>
#include <cstring>
#include <new>
#include <string>
#include <utility>
#include <vector>

struct kd_ngram_scorer { kana_decoder::MaskedNgramScorer impl; };
struct kd_engine {
    kana_decoder::LexiconTrie lexicon;
    kana_decoder::MaskedNgramScorer scorer;
    kana_decoder::ConnectionModel connections;
    bool loaded = false;
};
struct kd_decode_result { std::vector<kana_decoder::DecodedPath> paths; };
struct kd_streaming_session {
    kana_decoder::LexiconTrie lexicon;
    kana_decoder::MaskedNgramScorer scorer;
    kana_decoder::ConnectionModel connections;
    kana_decoder::StreamingSession session;
    kana_decoder::StreamingSnapshot snapshot;
    explicit kd_streaming_session(std::size_t limit) : session(limit) {}
};

extern "C" {
uint32_t kd_c_api_version(void) { return KD_C_API_VERSION; }
kd_ngram_scorer* kd_ngram_scorer_create(void) {
    return new (std::nothrow) kd_ngram_scorer();
}
void kd_ngram_scorer_destroy(kd_ngram_scorer* p) { delete p; }

kd_engine* kd_engine_create(void) { return new (std::nothrow) kd_engine(); }
void kd_engine_destroy(kd_engine* p) { delete p; }

int kd_engine_load_pack(kd_engine* p, const char* dir) {
    if (!p || !dir || !*dir) return KD_ERR_INVALID_ARGUMENT;
    try {
        kana_decoder::LexiconTrie lex;
        kana_decoder::MaskedNgramScorer scorer;
        kana_decoder::ConnectionModel connections;
        if (!kana_decoder::load_language_pack(dir, lex, scorer, nullptr, &connections))
            return KD_ERR_INVALID_ARGUMENT;
        p->lexicon = std::move(lex);
        p->scorer = std::move(scorer);
        p->connections = std::move(connections);
        p->loaded = true;
        return 0;
    } catch (...) {
        return KD_ERR_INTERNAL;
    }
}

int kd_engine_decode(kd_engine* p, const char* reading, char* out, size_t cap) {
    if (!p || !p->loaded || !reading || !out || !cap) return KD_ERR_INVALID_ARGUMENT;
    try {
        auto paths = kana_decoder::decode_reading(reading, p->lexicon, p->scorer, {}, &p->connections);
        if (paths.empty() || paths.front().tokens.empty()) return KD_ERR_INVALID_ARGUMENT;
        std::string result;
        for (const auto& token : paths.front().tokens) result += token.candidate.surface;
        if (result.size() + 1 > cap) return KD_ERR_BUFFER_TOO_SMALL;
        std::memcpy(out, result.c_str(), result.size() + 1);
        return 0;
    } catch (...) {
        return KD_ERR_INTERNAL;
    }
}



kd_decode_result* kd_engine_decode_nbest(const kd_engine* engine,const char* reading,size_t max_paths) {
    if (!engine || !engine->loaded || !reading || !*reading || max_paths>8) return nullptr;
    try {
        kana_decoder::DecoderOptions options;
        options.max_paths=max_paths ? max_paths : 8;
        options.beam_width=std::max<std::size_t>(options.beam_width,options.max_paths);
        auto paths=kana_decoder::decode_reading(reading,engine->lexicon,engine->scorer,options,&engine->connections);
        if (paths.empty()) return nullptr;
        auto* result=new (std::nothrow) kd_decode_result();
        if (!result) return nullptr;
        result->paths=std::move(paths);
        return result;
    } catch (...) { return nullptr; }
}
void kd_decode_result_destroy(kd_decode_result* result) { delete result; }
size_t kd_decode_result_path_count(const kd_decode_result* result) {
    return result ? result->paths.size() : 0;
}
int kd_decode_result_path_score(const kd_decode_result* result,size_t index,double* score) {
    if (!result || !score || index>=result->paths.size()) return KD_ERR_INVALID_ARGUMENT;
    *score=result->paths[index].score; return 0;
}
size_t kd_decode_result_token_count(const kd_decode_result* result,size_t path_index) {
    return result && path_index<result->paths.size() ? result->paths[path_index].tokens.size() : 0;
}
int kd_decode_result_token(const kd_decode_result* result,size_t path_index,size_t token_index,kd_token_view* view) {
    if (!result || !view || path_index>=result->paths.size() || token_index>=result->paths[path_index].tokens.size())
        return KD_ERR_INVALID_ARGUMENT;
    const auto& token=result->paths[path_index].tokens[token_index];
    view->start_codepoint=token.start; view->end_codepoint=token.end;
    view->candidate_id=token.candidate.id.c_str(); view->surface=token.candidate.surface.c_str();
    view->pos=token.candidate.pos.c_str(); view->inflection_type=token.candidate.inflection_type.c_str();
    view->inflection_form=token.candidate.inflection_form.c_str();
    view->morphology_score=token.morphology_score; view->context_score=token.context_score;
    return 0;
}

kd_streaming_session* kd_streaming_session_create(const kd_engine* engine,size_t max_reading_codepoints) {
    if (!engine || !engine->loaded) return nullptr;
    try {
        auto* result = new (std::nothrow) kd_streaming_session(max_reading_codepoints ? max_reading_codepoints : 256);
        if (!result) return nullptr;
        result->lexicon = engine->lexicon;
        result->scorer = engine->scorer;
        result->connections = engine->connections;
        return result;
    } catch (...) { return nullptr; }
}
void kd_streaming_session_destroy(kd_streaming_session* p) { delete p; }

static int copy_stream_output(const std::string& value,char* output,size_t capacity) {
    if (!output || !capacity) return KD_ERR_INVALID_ARGUMENT;
    if (value.size()+1>capacity) return KD_ERR_BUFFER_TOO_SMALL;
    std::memcpy(output,value.c_str(),value.size()+1);
    return 0;
}
static std::string best_surface(const kana_decoder::StreamingSnapshot& snapshot) {
    if (snapshot.paths.empty()) return {};
    std::string result;
    for (const auto& token : snapshot.paths.front().tokens) result += token.candidate.surface;
    return result;
}
int kd_streaming_append(kd_streaming_session* p,const char* chunk,char* output,size_t capacity,uint64_t* revision) {
    if (!p || !chunk || !*chunk || !output || !capacity) return KD_ERR_INVALID_ARGUMENT;
    try {
        auto trial_session=p->session;
        auto trial_snapshot=p->snapshot;
        if (!trial_session.append(chunk,p->lexicon,p->scorer,trial_snapshot,{},&p->connections))
            return KD_ERR_INVALID_ARGUMENT;
        const std::string result=best_surface(trial_snapshot);
        const int copied=copy_stream_output(result,output,capacity);
        if (copied) return copied;
        p->session=std::move(trial_session);
        p->snapshot=std::move(trial_snapshot);
        if (revision) *revision=p->snapshot.revision;
        return 0;
    } catch (...) { return KD_ERR_INTERNAL; }
}
int kd_streaming_commit_common_prefix(kd_streaming_session* p,char* output,size_t capacity,uint64_t* revision) {
    if (!p || !output || !capacity) return KD_ERR_INVALID_ARGUMENT;
    try {
        auto trial_session=p->session;
        auto trial_snapshot=p->snapshot;
        std::string committed;
        if (!trial_session.commit_common_prefix(trial_snapshot,committed)) return KD_ERR_INVALID_ARGUMENT;
        const int copied=copy_stream_output(committed,output,capacity);
        if (copied) return copied;
        p->session=std::move(trial_session);
        p->snapshot=std::move(trial_snapshot);
        if (revision) *revision=p->snapshot.revision;
        return 0;
    } catch (...) { return KD_ERR_INTERNAL; }
}
int kd_streaming_commit_best(kd_streaming_session* p,char* output,size_t capacity,uint64_t* revision) {
    if (!p || !output || !capacity) return KD_ERR_INVALID_ARGUMENT;
    try {
        auto trial_session=p->session;
        auto trial_snapshot=p->snapshot;
        std::string committed;
        if (!trial_session.commit_best(trial_snapshot,committed)) return KD_ERR_INVALID_ARGUMENT;
        const int copied=copy_stream_output(committed,output,capacity);
        if (copied) return copied;
        p->session=std::move(trial_session);
        p->snapshot=std::move(trial_snapshot);
        if (revision) *revision=p->snapshot.revision;
        return 0;
    } catch (...) { return KD_ERR_INTERNAL; }
}
int kd_streaming_reset(kd_streaming_session* p) {
    if (!p) return KD_ERR_INVALID_ARGUMENT;
    try { p->session.reset(); p->snapshot={}; return 0; }
    catch (...) { return KD_ERR_INTERNAL; }
}

int kd_ngram_add_unigram(kd_ngram_scorer* p, const char* id, uint32_t n) {
    if (!p || !id) return KD_ERR_INVALID_ARGUMENT;
    try { p->impl.add_unigram(id, n); return 0; }
    catch (...) { return KD_ERR_INTERNAL; }
}
int kd_ngram_add_left_bigram(kd_ngram_scorer* p, const char* left, const char* id, uint32_t n) {
    if (!p || !left || !id) return KD_ERR_INVALID_ARGUMENT;
    try { p->impl.add_left_bigram(left, id, n); return 0; }
    catch (...) { return KD_ERR_INTERNAL; }
}
int kd_ngram_add_right_bigram(kd_ngram_scorer* p, const char* id, const char* right, uint32_t n) {
    if (!p || !right || !id) return KD_ERR_INVALID_ARGUMENT;
    try { p->impl.add_right_bigram(id, right, n); return 0; }
    catch (...) { return KD_ERR_INTERNAL; }
}

int kd_ngram_resolve(kd_ngram_scorer* p, const kd_candidate_view* c, size_t count,
                     const char* const* left, size_t nl, const char* const* right, size_t nr,
                     int mode, double min_score, double min_margin, int other,
                     const char* original, char* out, size_t cap) {
    if (!p || !c || !count || count > 8 || !out || !cap || mode < 0 || mode > 3)
        return KD_ERR_INVALID_ARGUMENT;
    try {
        std::vector<kana_decoder::Candidate> candidates;
        std::vector<std::string> ls, rs;
        for (size_t i = 0; i < count; ++i) {
            if (!c[i].id || !c[i].surface) return KD_ERR_INVALID_ARGUMENT;
            candidates.push_back({c[i].id, c[i].surface});
        }
        for (size_t i = 0; i < nl; ++i) {
            if (!left || !left[i]) return KD_ERR_INVALID_ARGUMENT;
            ls.emplace_back(left[i]);
        }
        for (size_t i = 0; i < nr; ++i) {
            if (!right || !right[i]) return KD_ERR_INVALID_ARGUMENT;
            rs.emplace_back(right[i]);
        }
        auto result = p->impl.resolve(candidates, ls, rs,
            static_cast<kana_decoder::OutputMode>(mode), min_score, min_margin,
            other != 0, original ? original : "");
        if (result.output.size() + 1 > cap) return KD_ERR_BUFFER_TOO_SMALL;
        std::memcpy(out, result.output.c_str(), result.output.size() + 1);
        return 0;
    } catch (...) {
        return KD_ERR_INTERNAL;
    }
}
}
