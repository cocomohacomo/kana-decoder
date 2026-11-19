#include "kana_decoder/c_api.h"
#include "kana_decoder/engine.hpp"
#include "kana_decoder/language_pack.hpp"
#include "kana_decoder/surge_runtime.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <cstring>
#include <new>
#include <string>
#include <utility>
#include <vector>

struct kd_ngram_scorer { kana_decoder::MaskedNgramScorer impl; };
struct kd_engine {
    std::shared_ptr<const kana_decoder::LanguagePack> pack;
    std::unique_ptr<kana_decoder::SurgeNormalizationRuntime> surge;
    bool loaded = false;
};
struct kd_decode_result {
    std::vector<kana_decoder::DecodedPath> paths;
    std::vector<std::string> normalized_texts;
};
struct kd_streaming_session {
    std::shared_ptr<const kana_decoder::LanguagePack> pack;
    kana_decoder::StreamingSession session;
    kana_decoder::StreamingSnapshot snapshot;
    std::unique_ptr<kana_decoder::SurgeNormalizationRuntime> surge;
    explicit kd_streaming_session(std::size_t limit) : session(limit) {}
};

namespace {
std::string temporary_surge_journal_path() {
    static std::atomic<std::uint64_t> counter{0};
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto name = "kana-decoder-surge-" + std::to_string(ticks) + "-" +
        std::to_string(counter.fetch_add(1, std::memory_order_relaxed)) + ".journal";
    return (std::filesystem::temp_directory_path() / name).string();
}
std::unique_ptr<kana_decoder::SurgeNormalizationRuntime> make_surge_runtime(
    const std::string& path, const std::string& context, bool remove_on_destroy) {
    kana_decoder::SurgeRuntimeConfig config;
    config.journal_path = path;
    config.context_id = context;
    config.remove_journal_on_destroy = remove_on_destroy;
    auto runtime = std::make_unique<kana_decoder::SurgeNormalizationRuntime>(std::move(config));
    std::string error;
    if (!runtime->open(error)) return {};
    return runtime;
}
}

extern "C" {
uint32_t kd_c_api_version(void) { return KD_C_API_VERSION; }
kd_ngram_scorer* kd_ngram_scorer_create(void) {
    return new (std::nothrow) kd_ngram_scorer();
}
void kd_ngram_scorer_destroy(kd_ngram_scorer* p) { delete p; }

kd_engine* kd_engine_create(void) {
    try {
        auto* engine = new (std::nothrow) kd_engine();
        if (!engine) return nullptr;
        const auto path = temporary_surge_journal_path();
        engine->surge = make_surge_runtime(path, path, true);
        if (!engine->surge) { delete engine; return nullptr; }
        return engine;
    } catch (...) { return nullptr; }
}
void kd_engine_destroy(kd_engine* p) { delete p; }
int kd_engine_set_surge_journal(kd_engine* p, const char* path) {
    if (!p || !path || !*path) return KD_ERR_INVALID_ARGUMENT;
    try {
        auto runtime = make_surge_runtime(path, path, false);
        if (!runtime) return KD_ERR_INTERNAL;
        if (p->pack && !runtime->set_language_adapter(&p->pack->adapter()))
            return KD_ERR_INTERNAL;
        p->surge = std::move(runtime);
        return 0;
    } catch (...) { return KD_ERR_INTERNAL; }
}

int kd_engine_load_pack(kd_engine* p, const char* dir) {
    if (!p || !dir || !*dir || p->loaded) return KD_ERR_INVALID_ARGUMENT;
    try {
        std::string error;
        auto pack = kana_decoder::load_language_pack_snapshot(dir, &error);
        if (!pack) return KD_ERR_INVALID_ARGUMENT;
        if (!p->surge || !p->surge->set_language_adapter(&pack->adapter()))
            return KD_ERR_INTERNAL;
        p->pack = std::move(pack);
        p->loaded = true;
        return 0;
    } catch (...) {
        return KD_ERR_INTERNAL;
    }
}

int kd_engine_decode(kd_engine* p, const char* reading, char* out, size_t cap) {
    if (!p || !p->loaded || !reading || !out || !cap) return KD_ERR_INVALID_ARGUMENT;
    try {
        kana_decoder::DecoderOptions options;
        options.max_paths = 8;
        options.beam_width = 8;
        auto paths = p->pack->decode(reading, options);
        if (paths.empty() || paths.front().tokens.empty() || !p->surge) return KD_ERR_INVALID_ARGUMENT;
        const auto normalized = p->surge->normalize(reading, paths, false);
        if (!normalized.committed) return KD_ERR_INTERNAL;
        const std::string& result = normalized.output;
        if (result.size() + 1 > cap) return KD_ERR_BUFFER_TOO_SMALL;
        std::string surge_error;
        if (!p->surge->commit_output(result, normalized.metrics.state_version, true, surge_error))
            return KD_ERR_INTERNAL;
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
        auto paths=engine->pack->decode(reading,options);
        if (paths.empty() || !engine->surge) return nullptr;
        const auto normalized = engine->surge->normalize(reading, paths, true);
        if (!normalized.committed) return nullptr;
        auto* result=new (std::nothrow) kd_decode_result();
        if (!result) return nullptr;
        result->paths=std::move(paths);
        result->normalized_texts.reserve(result->paths.size());
        for (std::size_t i = 0; i < result->paths.size(); ++i) {
            if (i == 0) {
                result->normalized_texts.push_back(normalized.output);
            } else {
                std::string text;
                for (const auto& token : result->paths[i].tokens) text += token.candidate.surface;
                result->normalized_texts.push_back(std::move(text));
            }
        }
        return result;
    } catch (...) { return nullptr; }
}
void kd_decode_result_destroy(kd_decode_result* result) { delete result; }
size_t kd_decode_result_path_count(const kd_decode_result* result) {
    return result ? result->paths.size() : 0;
}
int kd_decode_result_text(const kd_decode_result* result, size_t path_index,
                          char* output, size_t capacity) {
    if (!result || !output || capacity == 0 || path_index >= result->normalized_texts.size())
        return KD_ERR_INVALID_ARGUMENT;
    const auto& value = result->normalized_texts[path_index];
    if (value.size() + 1 > capacity) return KD_ERR_BUFFER_TOO_SMALL;
    std::memcpy(output, value.c_str(), value.size() + 1);
    return 0;
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
        result->pack = engine->pack;
        const auto path = temporary_surge_journal_path();
        result->surge = make_surge_runtime(path, path, true);
        if (!result->surge ||
            !result->surge->set_language_adapter(&result->pack->adapter())) {
            delete result;
            return nullptr;
        }
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
int kd_streaming_append(kd_streaming_session* p,const char* chunk,char* output,size_t capacity,uint64_t* revision) {
    if (!p || !chunk || !*chunk || !output || !capacity) return KD_ERR_INVALID_ARGUMENT;
    try {
        auto trial_session=p->session;
        auto trial_snapshot=p->snapshot;
        if (!p->pack->append(trial_session,chunk,trial_snapshot,{}))
            return KD_ERR_INVALID_ARGUMENT;
        if (!p->surge) return KD_ERR_INTERNAL;
        const auto normalized = p->surge->normalize(trial_snapshot.reading, trial_snapshot.paths, false);
        if (!normalized.committed) return KD_ERR_INTERNAL;
        const std::string result=normalized.output;
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
        if (!p->surge) return KD_ERR_INTERNAL;
        std::string surge_error;
        if (committed.size() + 1 > capacity) return KD_ERR_BUFFER_TOO_SMALL;
        if (!p->surge->commit_output(committed, trial_snapshot.revision, false, surge_error)) return KD_ERR_INTERNAL;
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
        if (!p->surge) return KD_ERR_INTERNAL;
        std::string surge_error;
        if (committed.size() + 1 > capacity) return KD_ERR_BUFFER_TOO_SMALL;
        if (!p->surge->commit_output(committed, trial_snapshot.revision, true, surge_error)) return KD_ERR_INTERNAL;
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
    try {
        // A reset starts a new recognition context, not merely an empty input
        // buffer. Replace the ephemeral SURGE journal so old Claims, Questions,
        // Focus and Chaos hypotheses cannot leak into the next session.
        const auto path = temporary_surge_journal_path();
        auto fresh_surge = make_surge_runtime(path, path, true);
        if (!fresh_surge ||
            !fresh_surge->set_language_adapter(&p->pack->adapter()))
            return KD_ERR_INTERNAL;
        p->surge = std::move(fresh_surge);
        p->session.reset();
        p->snapshot = {};
        return 0;
    } catch (...) { return KD_ERR_INTERNAL; }
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
            candidates.push_back({c[i].id, c[i].surface, {}, {}, {}});
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
