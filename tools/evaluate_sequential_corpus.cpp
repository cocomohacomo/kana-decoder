#include "kana_decoder/engine.hpp"
#include "kana_decoder/japanese_decoder.hpp"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <iomanip>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {
struct GoldToken {
    std::string reading;
    std::string surface;
    std::size_t start = 0;
    std::size_t end = 0;
};
struct Sentence {
    std::string document_id;
    std::string sentence_id;
    std::string domain;
    std::vector<GoldToken> tokens;
    std::string reading;
    std::string expected;
};
struct Metrics {
    std::size_t cases = 0;
    std::size_t exact = 0;
    std::size_t edit_distance = 0;
    std::size_t reference_codepoints = 0;
    std::size_t decode_failures = 0;
    std::size_t predicted_tokens = 0;
    std::size_t aligned_tokens = 0;
    std::size_t gold_tokens = 0;
    std::size_t over_limit = 0;
};

std::vector<std::string> split_tabs(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t begin = 0;
    for (;;) {
        const auto end = line.find('\t', begin);
        fields.push_back(line.substr(begin, end == std::string::npos
            ? std::string::npos : end - begin));
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return fields;
}

std::vector<std::string> codepoints(const std::string& text) {
    std::vector<std::string> out;
    if (!kana_decoder::is_valid_utf8(text)) return out;
    for (std::size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i]);
        const std::size_t width = lead < 0x80 ? 1
            : (lead & 0xe0) == 0xc0 ? 2
            : (lead & 0xf0) == 0xe0 ? 3 : 4;
        out.push_back(text.substr(i, width));
        i += width;
    }
    return out;
}

std::size_t distance(const std::vector<std::string>& a,
                     const std::vector<std::string>& b) {
    std::vector<std::size_t> prev(b.size() + 1), cur(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) prev[j] = j;
    for (std::size_t i = 1; i <= a.size(); ++i) {
        cur[0] = i;
        for (std::size_t j = 1; j <= b.size(); ++j) {
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1,
                prev[j - 1] + (a[i - 1] == b[j - 1] ? 0U : 1U)});
        }
        prev.swap(cur);
    }
    return prev.back();
}

bool load_sentences(const std::string& path, const std::string& split,
                    std::vector<Sentence>& sentences, std::size_t& source_tokens) {
    std::ifstream input(path);
    if (!input) return false;
    std::map<std::pair<std::string, std::string>, std::size_t> index;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto fields = split_tabs(line);
        if (!fields.empty() && fields[0] == "document_id") continue;
        if (fields.size() != 6) return false;
        if (fields[5] != split) continue;
        if (fields[0].empty() || fields[1].empty() || fields[2].empty()
            || fields[3].empty() || !kana_decoder::is_valid_utf8(fields[2])
            || !kana_decoder::is_valid_utf8(fields[3])) return false;
        const auto key = std::make_pair(fields[0], fields[1]);
        auto found = index.find(key);
        if (found == index.end()) {
            const auto next = sentences.size();
            index.emplace(key, next);
            sentences.push_back(Sentence{fields[0], fields[1], fields[4], {}, {}, {}});
            found = index.find(key);
        }
        auto& sentence = sentences[found->second];
        if (sentence.domain != fields[4]) return false;
        const auto start = codepoints(sentence.reading).size();
        const auto width = codepoints(fields[2]).size();
        sentence.tokens.push_back(GoldToken{fields[2], fields[3], start, start + width});
        sentence.reading += fields[2];
        sentence.expected += fields[3];
        ++source_tokens;
    }
    return input.eof() && !sentences.empty();
}

void add_sequence_counts(const std::vector<std::string>& tokens,kana_decoder::MaskedNgramScorer& scorer) {
    for(std::size_t i=0;i<tokens.size();++i) {
        const auto& token=tokens[i]; if(token.empty()) continue;
        scorer.add_unigram(token,1);
        if(i>=1&&!tokens[i-1].empty()) scorer.add_left_bigram(tokens[i-1],token,1);
        if(i>=2&&!tokens[i-2].empty()&&!tokens[i-1].empty()) {
            scorer.add_left_ngram({tokens[i-2],tokens[i-1]},token,1);
            scorer.add_left_skipgram(tokens[i-2],token,1);
        }
        if(i>=3&&!tokens[i-3].empty()&&!tokens[i-2].empty()&&!tokens[i-1].empty())
            scorer.add_left_ngram({tokens[i-3],tokens[i-2],tokens[i-1]},token,1);
    }
}
bool load_training_sequences(const std::string& path,const kana_decoder::LexiconTrie& lexicon,
                             kana_decoder::MaskedNgramScorer& scorer,
                             kana_decoder::MaskedNgramScorer& pos_scorer) {
    std::ifstream input(path);
    if (!input) return false;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto tokens = split_tabs(line);
        add_sequence_counts(tokens,scorer);
        std::vector<std::string> pos_tokens;
        pos_tokens.reserve(tokens.size());
        for(const auto& token:tokens) pos_tokens.push_back(lexicon.candidate_pos(token));
        add_sequence_counts(pos_tokens,pos_scorer);
    }
    return input.eof();
}

void add_metrics(Metrics& target, const Metrics& source) {
    target.cases += source.cases;
    target.exact += source.exact;
    target.edit_distance += source.edit_distance;
    target.reference_codepoints += source.reference_codepoints;
    target.decode_failures += source.decode_failures;
    target.predicted_tokens += source.predicted_tokens;
    target.aligned_tokens += source.aligned_tokens;
    target.gold_tokens += source.gold_tokens;
    target.over_limit += source.over_limit;
}

std::string metric_reading_length_bucket(std::size_t length) {
    if (length <= 4) return "1-4";
    if (length <= 8) return "5-8";
    if (length <= 16) return "9-16";
    if (length <= 32) return "17-32";
    if (length <= 64) return "33-64";
    if (length <= 128) return "65-128";
    return "129-256";
}

void evaluate(const std::vector<Sentence>& sentences,
              const kana_decoder::LexiconTrie& lexicon,
              const kana_decoder::MaskedNgramScorer& scorer,
              const kana_decoder::ConnectionModel& connections,
              Metrics& metrics,
              const kana_decoder::DecoderOptions& options = {},
              std::map<std::string, Metrics>* by_domain = nullptr,
              std::map<std::string, Metrics>* by_reading_length = nullptr) {
    for (const auto& sentence : sentences) {
        Metrics current;
        ++current.cases;
        const auto reading_points = codepoints(sentence.reading);
        const auto expected_points = codepoints(sentence.expected);
        if (reading_points.size() > 256) {
            ++current.over_limit;
        } else {
            current.gold_tokens += sentence.tokens.size();
            const auto paths = kana_decoder::decode_reading(
                sentence.reading, lexicon, scorer, options, &connections);
            std::string predicted;
            if (paths.empty()) {
                ++current.decode_failures;
            } else {
                for (const auto& token : paths.front().tokens) {
                    predicted += token.candidate.surface;
                }
                current.predicted_tokens += paths.front().tokens.size();
                for (const auto& gold : sentence.tokens) {
                    const auto found = std::find_if(paths.front().tokens.begin(),
                        paths.front().tokens.end(), [&](const kana_decoder::DecodedToken& token) {
                            return token.start == gold.start && token.end == gold.end
                                && token.candidate.surface == gold.surface;
                        });
                    if (found != paths.front().tokens.end()) ++current.aligned_tokens;
                }
            }
            if (predicted == sentence.expected) ++current.exact;
            current.edit_distance += distance(codepoints(predicted), expected_points);
            current.reference_codepoints += expected_points.size();
        }
        add_metrics(metrics, current);
        if (by_domain) add_metrics((*by_domain)[sentence.domain], current);
        if (by_reading_length && !reading_points.empty()
            && reading_points.size() <= 256) {
            add_metrics((*by_reading_length)[metric_reading_length_bucket(reading_points.size())],
                        current);
        }
    }
}

struct CandidateSurvivalMetrics {
    std::size_t gold_tokens = 0;
    std::size_t lattice_present = 0;
    std::size_t morphology_shortlist = 0;
    std::size_t scored = 0;
    std::size_t beam_survived = 0;
    std::size_t beam_pruning_drop = 0;
    std::size_t final_selected = 0;
    std::size_t final_aligned = 0;
    std::size_t nbest_aligned = 0;
    std::size_t nbest_recoverable = 0;
    std::size_t scored_not_in_nbest = 0;
    std::size_t lattice_missing = 0;
    std::size_t shortlist_drop = 0;
    std::size_t expansion_drop = 0;
    std::size_t trace_censored = 0;
    std::size_t final_selection_drop = 0;
};

void add_candidate_survival_metrics(CandidateSurvivalMetrics& target,
                                    const CandidateSurvivalMetrics& source) {
#define KANA_ADD_METRIC(name) target.name += source.name
    KANA_ADD_METRIC(gold_tokens);
    KANA_ADD_METRIC(lattice_present);
    KANA_ADD_METRIC(morphology_shortlist);
    KANA_ADD_METRIC(scored);
    KANA_ADD_METRIC(beam_survived);
    KANA_ADD_METRIC(beam_pruning_drop);
    KANA_ADD_METRIC(final_selected);
    KANA_ADD_METRIC(final_aligned);
    KANA_ADD_METRIC(nbest_aligned);
    KANA_ADD_METRIC(nbest_recoverable);
    KANA_ADD_METRIC(scored_not_in_nbest);
    KANA_ADD_METRIC(lattice_missing);
    KANA_ADD_METRIC(shortlist_drop);
    KANA_ADD_METRIC(expansion_drop);
    KANA_ADD_METRIC(trace_censored);
    KANA_ADD_METRIC(final_selection_drop);
#undef KANA_ADD_METRIC
}

std::string reading_length_bucket(std::size_t length) {
    if (length <= 4) return "1-4";
    if (length <= 8) return "5-8";
    if (length <= 16) return "9-16";
    if (length <= 32) return "17-32";
    if (length <= 64) return "33-64";
    if (length <= 128) return "65-128";
    return "129-256";
}

void evaluate_candidate_survival(
    const std::vector<Sentence>& sentences,
    const kana_decoder::LexiconTrie& lexicon,
    const kana_decoder::MaskedNgramScorer& scorer,
    const kana_decoder::ConnectionModel& connections,
    const kana_decoder::DecoderOptions& options,
    CandidateSurvivalMetrics& metrics,
    std::map<std::string, CandidateSurvivalMetrics>& by_domain,
    std::map<std::string, CandidateSurvivalMetrics>& by_reading_length) {
    for (const auto& sentence : sentences) {
        const auto reading_length = codepoints(sentence.reading).size();
        if (reading_length > 256 || reading_length == 0) continue;
        CandidateSurvivalMetrics sentence_metrics;
        std::vector<kana_decoder::CandidateScoreTrace> score_trace;
        std::vector<kana_decoder::LatticePositionTrace> lattice_trace;
        const auto paths = kana_decoder::decode_reading(
            sentence.reading, lexicon, scorer, options, &connections,
            &score_trace, &lattice_trace);
        const auto& best = paths.empty() ? std::vector<kana_decoder::DecodedToken>{}
                                         : paths.front().tokens;
        const bool score_trace_capped = score_trace.size() >= options.max_trace_records;
        for (const auto& gold : sentence.tokens) {
            ++sentence_metrics.gold_tokens;
            bool in_lattice = false;
            bool in_shortlist = false;
            for (const auto& position : lattice_trace) {
                if (position.start > gold.start) break;
                for (const auto& edge : position.dictionary_matches) {
                    if (edge.start != gold.start || edge.end != gold.end) continue;
                    std::vector<const kana_decoder::LatticeCandidate*> ranked;
                    for (const auto& candidate : edge.candidates) {
                        if (candidate.surface == gold.surface) in_lattice = true;
                        ranked.push_back(&candidate);
                    }
                    std::stable_sort(ranked.begin(), ranked.end(),
                        [](const auto* a, const auto* b) {
                            return static_cast<std::int64_t>(a->word_cost) + a->connection_cost
                                 < static_cast<std::int64_t>(b->word_cost) + b->connection_cost;
                        });
                    const auto limit = std::min<std::size_t>(8, ranked.size());
                    for (std::size_t i = 0; i < limit; ++i)
                        if (ranked[i]->surface == gold.surface) in_shortlist = true;
                }
            }
            bool was_scored = false;
            bool survived_beam = false;
            for (const auto& item : score_trace) {
                if (item.start == gold.start && item.end == gold.end
                    && item.surface == gold.surface) {
                    was_scored = true;
                    survived_beam = survived_beam || item.beam_survived;
                }
            }
            const auto matches_gold = [&](const kana_decoder::DecodedPath& path) {
                return std::any_of(path.tokens.begin(), path.tokens.end(),
                    [&](const kana_decoder::DecodedToken& token) {
                        return token.start == gold.start && token.end == gold.end
                            && token.candidate.surface == gold.surface;
                    });
            };
            const bool aligned = std::any_of(best.begin(), best.end(),
                [&](const kana_decoder::DecodedToken& token) {
                    return token.start == gold.start && token.end == gold.end
                        && token.candidate.surface == gold.surface;
                });
            const bool present_in_nbest = std::any_of(paths.begin(), paths.end(), matches_gold);
            if (in_lattice) ++sentence_metrics.lattice_present;
            if (in_shortlist) ++sentence_metrics.morphology_shortlist;
            if (was_scored) ++sentence_metrics.scored;
            if (survived_beam) ++sentence_metrics.beam_survived;
            if (was_scored && !survived_beam) ++sentence_metrics.beam_pruning_drop;
            if (aligned) ++sentence_metrics.final_aligned;
            if (present_in_nbest) ++sentence_metrics.nbest_aligned;
            if (present_in_nbest && !aligned) ++sentence_metrics.nbest_recoverable;
            if (was_scored && !present_in_nbest) ++sentence_metrics.scored_not_in_nbest;
            if (!in_lattice) ++sentence_metrics.lattice_missing;
            else if (!in_shortlist) ++sentence_metrics.shortlist_drop;
            else if (!was_scored && score_trace_capped) ++sentence_metrics.trace_censored;
            else if (!was_scored) ++sentence_metrics.expansion_drop;
            else if (!survived_beam) { /* counted in beam_pruning_drop above */ }
            else if (!aligned) ++sentence_metrics.final_selection_drop;
            else ++sentence_metrics.final_selected;
        }
        add_candidate_survival_metrics(metrics, sentence_metrics);
        add_candidate_survival_metrics(by_domain[sentence.domain], sentence_metrics);
        add_candidate_survival_metrics(by_reading_length[reading_length_bucket(reading_length)], sentence_metrics);
    }
}

void print_candidate_survival_summary(const CandidateSurvivalMetrics& m) {
    const auto pct = [&](std::size_t n) {
        return m.gold_tokens ? 100.0 * static_cast<double>(n) / m.gold_tokens : 0.0;
    };
    const auto classified = m.lattice_missing + m.shortlist_drop
        + m.expansion_drop + m.trace_censored + m.beam_pruning_drop
        + m.final_selection_drop + m.final_selected;
    const std::pair<const char*, std::size_t> losses[] = {
        {"lattice_missing", m.lattice_missing},
        {"shortlist_drop", m.shortlist_drop},
        {"expansion_drop", m.expansion_drop},
        {"trace_censored", m.trace_censored},
        {"beam_pruning_drop", m.beam_pruning_drop},
        {"final_selection_drop", m.final_selection_drop},
    };
    const std::pair<const char*, std::size_t>* dominant = nullptr;
    for (const auto& loss : losses) {
        if (!dominant || loss.second > dominant->second) dominant = &loss;
    }
    const char* dominant_stage = dominant && dominant->second
        ? dominant->first : "none";
    const std::size_t dominant_count = dominant ? dominant->second : 0;
    std::cout << "{"
        << "\"dominant_loss_stage\":" << std::quoted(dominant_stage)
        << ",\"dominant_loss_tokens\":" << dominant_count
        << ",\"dominant_loss_pct\":" << pct(dominant_count)
        << ",\"loss_stage_rates_pct\":{"
        << "\"lattice_missing\":" << pct(m.lattice_missing)
        << ",\"shortlist_drop\":" << pct(m.shortlist_drop)
        << ",\"expansion_drop\":" << pct(m.expansion_drop)
        << ",\"trace_censored\":" << pct(m.trace_censored)
        << ",\"beam_pruning_drop\":" << pct(m.beam_pruning_drop)
        << ",\"final_selection_drop\":" << pct(m.final_selection_drop)
        << ",\"final_selected\":" << pct(m.final_selected) << "}"
        << ",\"classified_gold_tokens\":" << classified
        << ",\"unclassified_gold_tokens\":" << (m.gold_tokens - std::min(m.gold_tokens, classified))
        << ",\"gold_tokens\":" << m.gold_tokens
        << ",\"lattice_present\":" << m.lattice_present
        << ",\"lattice_coverage_pct\":" << pct(m.lattice_present)
        << ",\"morphology_top8\":" << m.morphology_shortlist
        << ",\"morphology_top8_coverage_pct\":" << pct(m.morphology_shortlist)
        << ",\"scored_or_expanded\":" << m.scored
        << ",\"scored_coverage_pct\":" << pct(m.scored)
        << ",\"beam_survived\":" << m.beam_survived
        << ",\"beam_pruning_drop\":" << m.beam_pruning_drop
        << ",\"final_selected\":" << m.final_selected
        << ",\"final_aligned\":" << m.final_aligned
        << ",\"final_alignment_pct\":" << pct(m.final_aligned)
        << ",\"nbest_aligned\":" << m.nbest_aligned
        << ",\"nbest_alignment_pct\":" << pct(m.nbest_aligned)
        << ",\"nbest_recoverable\":" << m.nbest_recoverable
        << ",\"scored_not_in_nbest\":" << m.scored_not_in_nbest
        << ",\"lattice_missing\":" << m.lattice_missing
        << ",\"shortlist_drop\":" << m.shortlist_drop
        << ",\"expansion_drop\":" << m.expansion_drop
        << ",\"trace_censored\":" << m.trace_censored
        << ",\"final_selection_drop\":" << m.final_selection_drop << "}";
}

void print_candidate_survival_groups(
    const char* name,
    const std::map<std::string, CandidateSurvivalMetrics>& groups) {
    std::cout << ",\"" << name << "\":{";
    bool first = true;
    for (const auto& entry : groups) {
        if (!first) std::cout << ",";
        first = false;
        std::cout << std::quoted(entry.first) << ":";
        print_candidate_survival_summary(entry.second);
    }
    std::cout << "}";
}

void print_metric_groups(
    const char* name, const std::map<std::string, Metrics>& groups) {
    std::cout << ",\"" << name << "\":{";
    bool first = true;
    for (const auto& entry : groups) {
        if (!first) std::cout << ",";
        first = false;
        const auto& m = entry.second;
        const auto scored = m.cases - m.over_limit;
        const double exact = scored ? static_cast<double>(m.exact) / scored : 0.0;
        const double cer = m.reference_codepoints
            ? static_cast<double>(m.edit_distance) / m.reference_codepoints : 0.0;
        const double precision = m.predicted_tokens
            ? static_cast<double>(m.aligned_tokens) / m.predicted_tokens : 0.0;
        const double recall = m.gold_tokens
            ? static_cast<double>(m.aligned_tokens) / m.gold_tokens : 0.0;
        std::cout << std::quoted(entry.first) << ":{"
            << "\"cases\":" << m.cases
            << ",\"scored_cases\":" << scored
            << ",\"exact_matches\":" << m.exact
            << ",\"exact_accuracy\":" << exact
            << ",\"edit_distance\":" << m.edit_distance
            << ",\"reference_codepoints\":" << m.reference_codepoints
            << ",\"cer\":" << cer
            << ",\"decode_failures\":" << m.decode_failures
            << ",\"over_limit_sentences\":" << m.over_limit
            << ",\"gold_tokens\":" << m.gold_tokens
            << ",\"predicted_tokens\":" << m.predicted_tokens
            << ",\"exact_token_alignment\":" << m.aligned_tokens
            << ",\"token_alignment_precision\":" << precision
            << ",\"token_alignment_recall\":" << recall << "}";
    }
    std::cout << "}";
}

double cer_of(const Metrics& m);
double exact_of(const Metrics& m);

void print_paired_comparison_groups(
    const char* name,
    const std::map<std::string, Metrics>& baseline,
    const std::map<std::string, Metrics>& surge) {
    std::cout << ",\""<< name << "\":{";
    bool first = true;
    for (const auto& entry : baseline) {
        const auto found = surge.find(entry.first);
        if (found == surge.end()) continue;
        if (!first) std::cout << ",";
        first = false;
        const auto& b = entry.second;
        const auto& s = found->second;
        const double cer_delta = cer_of(s) - cer_of(b);
        const double exact_delta = exact_of(s) - exact_of(b);
        const double baseline_recall = b.gold_tokens
            ? static_cast<double>(b.aligned_tokens) / b.gold_tokens : 0.0;
        const double surge_recall = s.gold_tokens
            ? static_cast<double>(s.aligned_tokens) / s.gold_tokens : 0.0;
        std::cout << std::quoted(entry.first) << ":{"
            << "\"cases\":" << std::min(b.cases, s.cases)
            << ",\"baseline_cer\":" << cer_of(b)
            << ",\"surge_cer\":" << cer_of(s)
            << ",\"cer_delta\":" << cer_delta
            << ",\"baseline_exact_accuracy\":" << exact_of(b)
            << ",\"surge_exact_accuracy\":" << exact_of(s)
            << ",\"exact_accuracy_delta\":" << exact_delta
            << ",\"baseline_token_alignment_recall\":" << baseline_recall
            << ",\"surge_token_alignment_recall\":" << surge_recall
            << ",\"token_alignment_recall_delta\":" << surge_recall - baseline_recall
            << ",\"baseline_edit_distance\":" << b.edit_distance
            << ",\"surge_edit_distance\":" << s.edit_distance
            << ",\"reference_codepoints\":" << std::min(b.reference_codepoints, s.reference_codepoints)
            << "}";
    }
    std::cout << "}";
}

void print_metrics(const char* name, const Metrics& m) {
    const auto scored = m.cases - m.over_limit;
    const double exact = scored ? static_cast<double>(m.exact) / scored : 0.0;
    const double cer = m.reference_codepoints
        ? static_cast<double>(m.edit_distance) / m.reference_codepoints : 0.0;
    const double precision = m.predicted_tokens
        ? static_cast<double>(m.aligned_tokens) / m.predicted_tokens : 0.0;
    const double recall = m.gold_tokens
        ? static_cast<double>(m.aligned_tokens) / m.gold_tokens : 0.0;
    std::cout << ",\""<< name << "\":{"
        << "\"cases\":" << m.cases
        << ",\"scored_cases\":" << scored
        << ",\"exact_matches\":" << m.exact
        << ",\"exact_accuracy\":" << exact
        << ",\"edit_distance\":" << m.edit_distance
        << ",\"reference_codepoints\":" << m.reference_codepoints
        << ",\"cer\":" << cer
        << ",\"decode_failures\":" << m.decode_failures
        << ",\"over_limit_sentences\":" << m.over_limit
        << ",\"gold_tokens\":" << m.gold_tokens
        << ",\"predicted_tokens\":" << m.predicted_tokens
        << ",\"exact_token_alignment\":" << m.aligned_tokens
        << ",\"token_alignment_precision\":" << precision
        << ",\"token_alignment_recall\":" << recall << "}";
}

double cer_of(const Metrics& m) {
    return m.reference_codepoints
        ? static_cast<double>(m.edit_distance) / m.reference_codepoints : 0.0;
}
double exact_of(const Metrics& m) {
    const auto scored = m.cases - m.over_limit;
    return scored ? static_cast<double>(m.exact) / scored : 0.0;
}
void print_bottleneck_comparison_groups(
    const char* name,
    const std::map<std::string, Metrics>& baseline,
    const std::map<std::string, Metrics>& surge,
    const std::map<std::string, CandidateSurvivalMetrics>& survival) {
    std::cout << ",\"" << name << "\":{";
    bool first = true;
    for (const auto& entry : baseline) {
        const auto si = surge.find(entry.first);
        const auto di = survival.find(entry.first);
        if (si == surge.end() || di == survival.end()) continue;
        if (!first) std::cout << ",";
        first = false;
        const auto& b = entry.second;
        const auto& s = si->second;
        const auto& d = di->second;
        const double br = b.gold_tokens ? static_cast<double>(b.aligned_tokens) / b.gold_tokens : 0.0;
        const double sr = s.gold_tokens ? static_cast<double>(s.aligned_tokens) / s.gold_tokens : 0.0;
        const std::pair<const char*, std::size_t> losses[] = {
            {"lattice_missing", d.lattice_missing}, {"shortlist_drop", d.shortlist_drop},
            {"expansion_drop", d.expansion_drop}, {"trace_censored", d.trace_censored},
            {"beam_pruning_drop", d.beam_pruning_drop},
            {"final_selection_drop", d.final_selection_drop},
        };
        const std::pair<const char*, std::size_t>* dominant = nullptr;
        for (const auto& loss : losses) if (!dominant || loss.second > dominant->second) dominant = &loss;
        const char* stage = dominant && dominant->second ? dominant->first : "none";
        const auto count = dominant ? dominant->second : 0;
        std::cout << std::quoted(entry.first) << ":{"
            << "\"cases\":" << std::min(b.cases, s.cases)
            << ",\"cer_delta\":" << cer_of(s) - cer_of(b)
            << ",\"exact_accuracy_delta\":" << exact_of(s) - exact_of(b)
            << ",\"token_alignment_recall_delta\":" << sr - br
            << ",\"dominant_loss_stage\":" << std::quoted(stage)
            << ",\"dominant_loss_tokens\":" << count
            << ",\"dominant_loss_pct\":"
            << (d.gold_tokens ? 100.0 * static_cast<double>(count) / d.gold_tokens : 0.0)
            << ",\"gold_tokens\":" << d.gold_tokens
            << ",\"loss_stage_rates_pct\":{";
        for (std::size_t i = 0; i < 6; ++i) {
            if (i) std::cout << ",";
            std::cout << std::quoted(losses[i].first) << ":"
                << (d.gold_tokens ? 100.0 * static_cast<double>(losses[i].second) / d.gold_tokens : 0.0);
        }
        std::cout << ",\"final_selected\":"
            << (d.gold_tokens ? 100.0 * static_cast<double>(d.final_selected) / d.gold_tokens : 0.0)
            << "}}";
    }
    std::cout << "}";
}

bool better_metrics(const Metrics& candidate, const Metrics& current) {
    const double a = cer_of(candidate), b = cer_of(current);
    if (std::abs(a - b) > 1e-12) return a < b;
    const double ae = exact_of(candidate), be = exact_of(current);
    if (std::abs(ae - be) > 1e-12) return ae > be;
    return candidate.aligned_tokens > current.aligned_tokens;
}
bool write_calibration(const std::string& path, double weight, double margin, double alpha, double pos_weight, double cluster_weight) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;
    out << std::setprecision(17) << weight << "\t" << margin << "\t" << alpha << "\t" << pos_weight << "\t" << cluster_weight << "\n";
    return static_cast<bool>(out);
}
bool read_calibration(const std::string& path, double& weight, double& margin, double& alpha, double& pos_weight, double& cluster_weight) {
    std::ifstream in(path);
    if (!(in >> weight >> margin >> alpha >> pos_weight >> cluster_weight)) return false;
    std::string trailing;
    if (in >> trailing) return false;
    return std::isfinite(weight) && weight >= 0.0
        && std::isfinite(margin) && margin >= 0.0
        && std::isfinite(alpha) && alpha > 0.0 && alpha <= 100.0
        && std::isfinite(pos_weight) && pos_weight >= 0.0 && pos_weight <= 10.0
        && std::isfinite(cluster_weight) && cluster_weight >= 0.0 && cluster_weight <= 10.0;
}
void print_calibration_metrics(const char* name, const Metrics& m) {
    std::cout << ",\"" << name << "\":{"
        << "\"cases\":" << m.cases
        << ",\"exact_matches\":" << m.exact
        << ",\"exact_accuracy\":" << exact_of(m)
        << ",\"edit_distance\":" << m.edit_distance
        << ",\"reference_codepoints\":" << m.reference_codepoints
        << ",\"cer\":" << cer_of(m)
        << ",\"decode_failures\":" << m.decode_failures
        << ",\"over_limit_sentences\":" << m.over_limit
        << ",\"gold_tokens\":" << m.gold_tokens
        << ",\"predicted_tokens\":" << m.predicted_tokens
        << ",\"exact_token_alignment\":" << m.aligned_tokens << "}";
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 4 && argc != 6 && argc != 7) {
        std::cerr << "usage: kana_decoder_sequential_eval PACK_DIR TRAIN_SEQUENCES.tsv TOKENIZED.tsv [dev-calibrate CALIBRATION.tsv | test CALIBRATION.tsv]\n";
        return 2;
    }
    const std::string mode = argc == 4 ? "test" : argv[4];
    if (mode != "test" && mode != "dev-calibrate") return 2;
    kana_decoder::LexiconTrie lexicon;
    kana_decoder::MaskedNgramScorer pack_scorer;
    kana_decoder::ConnectionModel connections;
    if (!kana_decoder::load_language_pack(argv[1], lexicon, pack_scorer,
                                          nullptr, &connections)) {
        std::cerr << "failed to load Language Pack\n";
        return 2;
    }
    // Build the optional POS lookup only for this corpus evaluator. Normal
    // decoder users do not pay the additional index memory cost.
    lexicon.build_candidate_pos_index();
    kana_decoder::MaskedNgramScorer trained_scorer, trained_pos_scorer;
    if (!load_training_sequences(argv[2], lexicon, trained_scorer, trained_pos_scorer)) {
        std::cerr << "failed to load training sequences\n";
        return 2;
    }
    std::vector<Sentence> sentences;
    std::size_t source_tokens = 0;
    const std::string split = mode == "dev-calibrate" ? "dev" : "test";
    if (!load_sentences(argv[3], split, sentences, source_tokens)) {
        std::cerr << "failed to load " << split << " token corpus\n";
        return 2;
    }
    kana_decoder::MaskedNgramScorer morphology_scorer;
    Metrics morphology, causal;
    std::map<std::string, Metrics> morphology_by_domain;
    std::map<std::string, Metrics> morphology_by_reading_length;
    evaluate(sentences, lexicon, morphology_scorer, connections, morphology, {},
             &morphology_by_domain, &morphology_by_reading_length);
    kana_decoder::DecoderOptions ngram_options;
    ngram_options.pos_ngram_scorer = &trained_pos_scorer;
    evaluate(sentences, lexicon, trained_scorer, connections, causal, ngram_options);
    kana_decoder::SurgeClusterModel cluster_model;
    const bool cluster_enabled = argc == 7;
    if (cluster_enabled && !cluster_model.load_tsv(argv[6])) {
        std::cerr << "failed to load SURGE cluster model\n";
        return 2;
    }

    if (mode == "dev-calibrate") {
        const double weights[] = {0.0, 1.0, 2.0};
        const double margins[] = {0.0, 0.1, 0.5};
        const double alphas[] = {0.1, 0.5};
        const double pos_weights[] = {0.0, 0.15};
        const double cluster_weights[] = {0.0, 0.5, 1.0, 2.0};
        Metrics best;
        double best_weight = 0.0, best_margin = 0.5, best_alpha = 0.5, best_pos_weight = 0.0, best_cluster_weight = 0.0;
        bool have_best = false;
        std::size_t configurations = 0;
        for (double weight : weights) {
            for (double margin : margins) {
              for (double alpha : alphas) {
              for (double pos_weight : pos_weights) {
              const std::size_t cluster_weight_count = cluster_enabled ? 4 : 1;
              for (std::size_t cluster_index = 0; cluster_index < cluster_weight_count; ++cluster_index) {
                const double cluster_weight = cluster_weights[cluster_index];
                if (!trained_scorer.set_smoothing_alpha(alpha) || !trained_pos_scorer.set_smoothing_alpha(alpha)) return 2;
                kana_decoder::DecoderOptions options;
                options.surge_left_context_gate = true;
                options.pos_ngram_scorer = &trained_pos_scorer;
                options.pos_context_weight = pos_weight;
                options.context_weight = weight;
                options.surge_cluster_model = cluster_enabled && cluster_weight > 0.0 ? &cluster_model : nullptr;
                options.surge_cluster_weight = cluster_weight;
                options.surge_context_margin = margin;
                Metrics candidate;
                evaluate(sentences, lexicon, trained_scorer, connections,
                         candidate, options);
                ++configurations;
                if (!have_best || better_metrics(candidate, best)) {
                    best = candidate;
                    best_weight = weight;
                    best_margin = margin;
                    best_alpha = alpha;
                    best_pos_weight = pos_weight;
                    best_cluster_weight = cluster_weight;
                    have_best = true;
                }
              }
              }
              }
            }
        }
        // Never deploy a dev configuration that is worse than morphology-only.
        if (!have_best || !better_metrics(best, morphology)) {
            best_weight = 0.0;
            best_margin = 0.5;
            best_alpha = 0.5;
            best_pos_weight = 0.0;
            best_cluster_weight = 0.0;
            best = morphology;
        }
        if (!write_calibration(argv[5], best_weight, best_margin, best_alpha, best_pos_weight, best_cluster_weight)) {
            std::cerr << "failed to write calibration file\n";
            return 2;
        }
        std::cout << "{\"mode\":\"dev_only_surge_calibration\""
                  << ",\"split\":\"dev\""
                  << ",\"test_split_used_for_selection\":false"
                  << ",\"configurations_evaluated\":" << configurations
                  << ",\"selected_context_weight\":" << best_weight
                  << ",\"selected_evidence_margin\":" << best_margin
                  << ",\"selected_smoothing_alpha\":" << best_alpha
                  << ",\"selected_pos_context_weight\":" << best_pos_weight
                  << ",\"selected_cluster_weight\":" << best_cluster_weight
                  << ",\"selected_policy\":\"" << (best_cluster_weight > 0.0 ? (best_weight > 0.0 ? "combined" : "cluster_only") : (best_weight > 0.0 ? "ngram_only" : "morphology")) << "\"";
        print_calibration_metrics("morphology_only", morphology);
        print_calibration_metrics("trained_unigram_left_context", causal);
        print_calibration_metrics("selected_surge_gate", best);
        std::cout << "}\n";
        return 0;
    }

    double selected_weight = 1.0, selected_margin = 0.5, selected_alpha = 0.5, selected_pos_weight = 0.0, selected_cluster_weight = 0.0;
    if (argc >= 6 && !read_calibration(argv[5], selected_weight, selected_margin, selected_alpha, selected_pos_weight, selected_cluster_weight)) {
        std::cerr << "invalid calibration file\n";
        return 2;
    }
    if (!trained_scorer.set_smoothing_alpha(selected_alpha)) return 2;
    kana_decoder::DecoderOptions surge_options;
    surge_options.surge_left_context_gate = true;
    surge_options.pos_ngram_scorer = &trained_pos_scorer;
    surge_options.pos_context_weight = selected_pos_weight;
    surge_options.context_weight = selected_weight;
    surge_options.surge_context_margin = selected_margin;
    Metrics surge;
    std::map<std::string, Metrics> surge_by_domain;
    std::map<std::string, Metrics> surge_by_reading_length;
    evaluate(sentences, lexicon, trained_scorer, connections, surge, surge_options,
             &surge_by_domain, &surge_by_reading_length);
    Metrics cluster, combined;
    if (cluster_enabled) {
        kana_decoder::DecoderOptions cluster_options;
        cluster_options.surge_cluster_model = &cluster_model;
        cluster_options.context_weight = 0.0;
        cluster_options.pos_context_weight = 0.0;
        evaluate(sentences, lexicon, trained_scorer, connections, cluster, cluster_options);
        kana_decoder::DecoderOptions combined_options;
        combined_options.surge_cluster_model = selected_cluster_weight > 0.0 ? &cluster_model : nullptr;
        combined_options.context_weight = selected_weight;
        combined_options.pos_ngram_scorer = &trained_pos_scorer;
        combined_options.pos_context_weight = selected_pos_weight;
        combined_options.surge_cluster_weight = selected_cluster_weight;
        evaluate(sentences, lexicon, trained_scorer, connections, combined, combined_options);
    }
    std::cout << "{\"mode\":\"end_to_end_sentence_reading_with_predicted_left_history\""
              << ",\"split\":\"test\""
              << ",\"gold_token_boundaries_used_for_decoding\":false"
              << ",\"gold_token_boundaries_used_for_alignment_only\":true"
              << ",\"future_context_used\":false"
              << ",\"dev_selected_context_weight\":" << selected_weight
              << ",\"dev_selected_evidence_margin\":" << selected_margin
              << ",\"dev_selected_smoothing_alpha\":" << selected_alpha
              << ",\"dev_selected_pos_context_weight\":" << selected_pos_weight
              << ",\"dev_selected_cluster_weight\":" << selected_cluster_weight
              << ",\"dev_selected_policy\":\"" << (selected_cluster_weight > 0.0 ? (selected_weight > 0.0 ? "combined" : "cluster_only") : (selected_weight > 0.0 ? "ngram_only" : "morphology")) << "\""

              << ",\"source_test_tokens\":" << source_tokens
              << ",\"sentences\":" << sentences.size();
    print_metrics("morphology_only", morphology);
    print_metric_groups("morphology_metrics_by_domain", morphology_by_domain);
    print_metric_groups("morphology_metrics_by_reading_length", morphology_by_reading_length);
    print_metrics("trained_unigram_left_context", causal);
    print_metrics("surge_evidence_gated_left_context", surge);
    print_metric_groups("surge_metrics_by_domain", surge_by_domain);
    print_metric_groups("surge_metrics_by_reading_length", surge_by_reading_length);
    print_paired_comparison_groups("surge_vs_morphology_by_domain",
                                   morphology_by_domain, surge_by_domain);
    print_paired_comparison_groups("surge_vs_morphology_by_reading_length",
                                   morphology_by_reading_length, surge_by_reading_length);
    if (cluster_enabled) {
        std::cout << ",\"surge_cluster_model\":{\"memberships\":" << cluster_model.membership_count()
                  << ",\"evidence_records\":" << cluster_model.evidence_count()
                  << ",\"gap_threshold\":" << cluster_model.gap_threshold() << "}";
        print_metrics("surge_cluster", cluster);
        print_metrics("dev_selected_surge_policy", combined);
    }
    CandidateSurvivalMetrics survival;
    std::map<std::string, CandidateSurvivalMetrics> survival_by_domain;
    std::map<std::string, CandidateSurvivalMetrics> survival_by_reading_length;
    kana_decoder::DecoderOptions survival_options;
    survival_options.surge_left_context_gate = true;
    survival_options.pos_ngram_scorer = &trained_pos_scorer;
    survival_options.pos_context_weight = selected_pos_weight;
    survival_options.context_weight = selected_weight;
    survival_options.surge_context_margin = selected_margin;
    survival_options.surge_cluster_model =
        cluster_enabled && selected_cluster_weight > 0.0 ? &cluster_model : nullptr;
    survival_options.surge_cluster_weight = selected_cluster_weight;
    evaluate_candidate_survival(sentences, lexicon, trained_scorer,
                                connections, survival_options, survival,
                                survival_by_domain, survival_by_reading_length);
    std::cout << ",\"candidate_survival_diagnostic\":";
    print_candidate_survival_summary(survival);
    print_candidate_survival_groups("candidate_survival_by_domain", survival_by_domain);
    print_candidate_survival_groups("candidate_survival_by_reading_length", survival_by_reading_length);
    print_bottleneck_comparison_groups("surge_bottleneck_by_domain",
        morphology_by_domain, surge_by_domain, survival_by_domain);
    print_bottleneck_comparison_groups("surge_bottleneck_by_reading_length",
        morphology_by_reading_length, surge_by_reading_length, survival_by_reading_length);
    std::cout << "}\n";
    return 0;
}
