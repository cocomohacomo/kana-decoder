#include "kana_decoder/engine.hpp"

#include <algorithm>
#include <cstdint>
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

bool load_sentences(const std::string& path, std::vector<Sentence>& sentences,
                    std::size_t& source_tokens) {
    std::ifstream input(path);
    if (!input) return false;
    std::map<std::pair<std::string, std::string>, std::size_t> index;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto fields = split_tabs(line);
        if (!fields.empty() && fields[0] == "document_id") continue;
        if (fields.size() != 6) return false;
        if (fields[5] != "test") continue;
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

bool load_training_sequences(const std::string& path,
                             kana_decoder::MaskedNgramScorer& scorer) {
    std::ifstream input(path);
    if (!input) return false;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto tokens = split_tabs(line);
        std::string previous;
        for (const auto& token : tokens) {
            if (token.empty()) continue;
            scorer.add_unigram(token, 1);
            if (!previous.empty()) scorer.add_left_bigram(previous, token, 1);
            previous = token;
        }
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
} // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "usage: kana_decoder_sequential_eval PACK_DIR TRAIN_SEQUENCES.tsv TEST_TOKENIZED.tsv\n";
        return 2;
    }
    kana_decoder::LexiconTrie lexicon;
    kana_decoder::MaskedNgramScorer pack_scorer;
    kana_decoder::ConnectionModel connections;
    if (!kana_decoder::load_language_pack(argv[1], lexicon, pack_scorer,
                                          nullptr, &connections)) {
        std::cerr << "failed to load Language Pack\n";
        return 2;
    }
    kana_decoder::MaskedNgramScorer trained_scorer;
    if (!load_training_sequences(argv[2], trained_scorer)) {
        std::cerr << "failed to load training sequences\n";
        return 2;
    }
    std::vector<Sentence> sentences;
    std::size_t source_tokens = 0;
    if (!load_sentences(argv[3], sentences, source_tokens)) {
        std::cerr << "failed to load test token corpus\n";
        return 2;
    }
    kana_decoder::MaskedNgramScorer morphology_scorer;
    Metrics morphology, causal, surge;
    evaluate(sentences, lexicon, morphology_scorer, connections, morphology);
    evaluate(sentences, lexicon, trained_scorer, connections, causal);
    kana_decoder::DecoderOptions surge_options;
    surge_options.surge_left_context_gate = true;
    surge_options.surge_context_margin = 0.5;
    evaluate(sentences, lexicon, trained_scorer, connections, surge, surge_options);
    std::cout << "{\"mode\":\"end_to_end_sentence_reading_with_predicted_left_history\""
              << ",\"gold_token_boundaries_used_for_decoding\":false"
              << ",\"gold_token_boundaries_used_for_alignment_only\":true"
              << ",\"future_context_used\":false"
              << ",\"source_test_tokens\":" << source_tokens
              << ",\"sentences\":" << sentences.size();
    print_metrics("morphology_only", morphology);
    print_metrics("trained_unigram_left_context", causal);
    print_metrics("surge_evidence_gated_left_context", surge);
    std::cout << "}\\n";
    return 0;
}
