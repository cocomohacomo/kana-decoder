#include "kana_decoder/engine.hpp"

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
            sentences.push_back(Sentence{fields[0], fields[1], {}, {}, {}});
            found = index.find(key);
        }
        auto& sentence = sentences[found->second];
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

void evaluate(const std::vector<Sentence>& sentences,
              const kana_decoder::LexiconTrie& lexicon,
              const kana_decoder::MaskedNgramScorer& scorer,
              const kana_decoder::ConnectionModel& connections,
              Metrics& metrics,
              const kana_decoder::DecoderOptions& options = {}) {
    for (const auto& sentence : sentences) {
        ++metrics.cases;
        const auto reading_points = codepoints(sentence.reading);
        const auto expected_points = codepoints(sentence.expected);
        if (reading_points.size() > 256) {
            ++metrics.over_limit;
            continue;
        }
        metrics.gold_tokens += sentence.tokens.size();
        const auto paths = kana_decoder::decode_reading(
            sentence.reading, lexicon, scorer, options, &connections);
        std::string predicted;
        if (paths.empty()) {
            ++metrics.decode_failures;
        } else {
            for (const auto& token : paths.front().tokens) {
                predicted += token.candidate.surface;
            }
            metrics.predicted_tokens += paths.front().tokens.size();
            for (const auto& gold : sentence.tokens) {
                const auto found = std::find_if(paths.front().tokens.begin(),
                    paths.front().tokens.end(), [&](const kana_decoder::DecodedToken& token) {
                        return token.start == gold.start && token.end == gold.end
                            && token.candidate.surface == gold.surface;
                    });
                if (found != paths.front().tokens.end()) ++metrics.aligned_tokens;
            }
        }
        if (predicted == sentence.expected) ++metrics.exact;
        metrics.edit_distance += distance(codepoints(predicted), expected_points);
        metrics.reference_codepoints += expected_points.size();
    }
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
    evaluate(sentences, lexicon, morphology_scorer, connections, morphology);
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
              for (double cluster_weight : cluster_weights) {
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
    evaluate(sentences, lexicon, trained_scorer, connections, surge, surge_options);
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
              << ",\"dev_selected_policy\":\"" << (selected_cluster_weight > 0.0 ? (selected_weight > 0.0 ? "combined" : "cluster_only") : (selected_weight > 0.0 ? "ngram_only" : "morphology")) << "\"";
              << ",\"source_test_tokens\":" << source_tokens
              << ",\"sentences\":" << sentences.size();
    print_metrics("morphology_only", morphology);
    print_metrics("trained_unigram_left_context", causal);
    print_metrics("surge_evidence_gated_left_context", surge);
    if (cluster_enabled) {
        std::cout << ",\"surge_cluster_model\":{\"memberships\":" << cluster_model.membership_count()
                  << ",\"evidence_records\":" << cluster_model.evidence_count()
                  << ",\"gap_threshold\":" << cluster_model.gap_threshold() << "}";
        print_metrics("surge_cluster", cluster);
        print_metrics("dev_selected_surge_policy", combined);
    }
    std::cout << "}\n";
    return 0;
}
