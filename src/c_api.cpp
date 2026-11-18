#include "kana_decoder/c_api.h"
#include "kana_decoder/engine.hpp"
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

extern "C" {
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
