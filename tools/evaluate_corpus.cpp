#include "kana_decoder/engine.hpp"
#include "kana_decoder/japanese_decoder.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {

using Codepoints = std::vector<std::string>;

struct Stats {
    std::size_t cases = 0;
    std::size_t exact_matches = 0;
    std::size_t edit_distance = 0;
    std::size_t reference_codepoints = 0;
    std::size_t decode_failures = 0;
};

struct EvaluationCase {
    std::string reading;
    std::string expected;
    std::string domain;
};

struct CaseResult {
    EvaluationCase input;
    std::string predicted;
    std::size_t edit_distance = 0;
    bool decode_failure = false;
    std::vector<kana_decoder::CandidateScoreTrace> trace;
    std::vector<kana_decoder::LatticePositionTrace> lattice_trace;
};

bool split_tsv_row(const std::string& line, EvaluationCase& row) {
    const auto first = line.find('\t');
    if (first == std::string::npos) return false;

    const auto second = line.find('\t', first + 1);
    row.reading = line.substr(0, first);
    row.expected = second == std::string::npos
        ? line.substr(first + 1)
        : line.substr(first + 1, second - first - 1);
    row.domain = second == std::string::npos
        ? "unspecified"
        : line.substr(second + 1);

    // Accept CRLF corpora without leaking the carriage return into domain labels.
    if (!row.domain.empty() && row.domain.back() == '\r') row.domain.pop_back();
    return !row.domain.empty() && row.domain.find('\t') == std::string::npos
        && kana_decoder::is_valid_utf8(row.reading)
        && kana_decoder::is_valid_utf8(row.expected)
        && kana_decoder::is_valid_utf8(row.domain);
}

Codepoints to_codepoints(const std::string& text) {
    Codepoints result;
    if (!kana_decoder::is_valid_utf8(text)) return result;

    for (std::size_t offset = 0; offset < text.size();) {
        const auto lead = static_cast<unsigned char>(text[offset]);
        const std::size_t width = lead < 0x80 ? 1
            : (lead & 0xe0) == 0xc0 ? 2
            : (lead & 0xf0) == 0xe0 ? 3 : 4;
        result.push_back(text.substr(offset, width));
        offset += width;
    }
    return result;
}

std::size_t levenshtein_distance(const Codepoints& lhs, const Codepoints& rhs) {
    std::vector<std::size_t> previous(rhs.size() + 1);
    std::vector<std::size_t> current(rhs.size() + 1);
    for (std::size_t column = 0; column <= rhs.size(); ++column) {
        previous[column] = column;
    }

    for (std::size_t row = 1; row <= lhs.size(); ++row) {
        current[0] = row;
        for (std::size_t column = 1; column <= rhs.size(); ++column) {
            const auto substitution = previous[column - 1]
                + (lhs[row - 1] == rhs[column - 1] ? 0U : 1U);
            current[column] = std::min({
                previous[column] + 1,
                current[column - 1] + 1,
                substitution
            });
        }
        previous.swap(current);
    }
    return previous.back();
}

std::string json_escape(const std::string& value) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string escaped;
    escaped.reserve(value.size() + 8);

    for (const unsigned char byte : value) {
        switch (byte) {
        case '"': escaped += "\\\""; break;
        case '\\': escaped += "\\\\"; break;
        case '\b': escaped += "\\b"; break;
        case '\f': escaped += "\\f"; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default:
            if (byte < 0x20) {
                escaped += "\\u00";
                escaped.push_back(hex[(byte >> 4) & 0x0f]);
                escaped.push_back(hex[byte & 0x0f]);
            } else {
                escaped.push_back(static_cast<char>(byte));
            }
        }
    }
    return escaped;
}

void accumulate(Stats& stats, const CaseResult& result) {
    ++stats.cases;
    stats.edit_distance += result.edit_distance;
    stats.reference_codepoints += to_codepoints(result.input.expected).size();
    if (result.predicted == result.input.expected) ++stats.exact_matches;
    if (result.decode_failure) ++stats.decode_failures;
}

void print_stats(const Stats& stats) {
    const double exact_accuracy = stats.cases
        ? static_cast<double>(stats.exact_matches) / stats.cases : 0.0;
    const double cer = stats.reference_codepoints
        ? static_cast<double>(stats.edit_distance) / stats.reference_codepoints
        : (stats.edit_distance ? 1.0 : 0.0);

    std::cout << "{\"cases\":" << stats.cases
              << ",\"exact_matches\":" << stats.exact_matches
              << ",\"exact_accuracy\":" << exact_accuracy
              << ",\"edit_distance\":" << stats.edit_distance
              << ",\"reference_codepoints\":" << stats.reference_codepoints
              << ",\"cer\":" << cer
              << ",\"decode_failures\":" << stats.decode_failures << "}";
}

void print_case_result(const CaseResult& result) {
    std::cout << "{\"reading\":\"" << json_escape(result.input.reading)
              << "\",\"expected\":\"" << json_escape(result.input.expected)
              << "\",\"predicted\":\"" << json_escape(result.predicted)
              << "\",\"domain\":\"" << json_escape(result.input.domain)
              << "\",\"edit_distance\":" << result.edit_distance
              << ",\"decode_failure\":" << (result.decode_failure ? "true" : "false");
    if (!result.trace.empty()) {
        std::cout << ",\"candidate_trace\":[";
        for (std::size_t i = 0; i < result.trace.size(); ++i) {
            if (i) std::cout << ',';
            const auto& item = result.trace[i];
            std::cout << "{\"start\":" << item.start
                      << ",\"end\":" << item.end
                      << ",\"candidate_id\":\"" << json_escape(item.candidate_id)
                      << "\",\"surface\":\"" << json_escape(item.surface)
                      << "\",\"word_cost\":" << item.word_cost
                      << ",\"candidate_connection_cost\":" << item.candidate_connection_cost
                      << ",\"transition_cost\":" << item.transition_cost
                      << ",\"raw_context_score\":" << item.raw_context_score
                      << ",\"context_evidence_score\":" << item.context_evidence_score
                      << ",\"context_weight\":" << item.context_weight
                      << ",\"morphology_score\":" << item.morphology_score
                      << ",\"weighted_context_score\":" << item.weighted_context_score
                      << ",\"local_score\":" << item.local_score
                      << ",\"unknown_fallback\":" << (item.unknown_fallback ? "true" : "false")
                      << "}";
        }
        std::cout << ']';
    }
    if (!result.lattice_trace.empty()) {
        std::cout << ",\"candidate_generation\":[";
        for (std::size_t i = 0; i < result.lattice_trace.size(); ++i) {
            if (i) std::cout << ',';
            const auto& position = result.lattice_trace[i];
            std::cout << "{\"start\":" << position.start
                      << ",\"dictionary_match_count\":" << position.dictionary_match_count
                      << ",\"fallback_enabled\":" << (position.fallback_enabled ? "true" : "false")
                      << ",\"fallback_generated\":" << (position.fallback_generated ? "true" : "false")
                      << ",\"fallback_suppressed_by_dictionary\":"
                      << (position.fallback_suppressed_by_dictionary ? "true" : "false")
                      << ",\"matches\":[";
            for (std::size_t j = 0; j < position.dictionary_matches.size(); ++j) {
                if (j) std::cout << ',';
                const auto& match = position.dictionary_matches[j];
                std::cout << "{\"start\":" << match.start
                          << ",\"end\":" << match.end
                          << ",\"candidates\":[";
                for (std::size_t k = 0; k < match.candidates.size(); ++k) {
                    if (k) std::cout << ',';
                    const auto& candidate = match.candidates[k];
                    std::cout << "{\"candidate_id\":\"" << json_escape(candidate.id)
                              << "\",\"surface\":\"" << json_escape(candidate.surface)
                              << "\"}";
                }
                std::cout << "]}";
            }
            std::cout << "]}";
        }
        std::cout << ']';
    }
    std::cout << "}";
}

bool evaluate_case(const EvaluationCase& input,
                   const kana_decoder::LexiconTrie& lexicon,
                   const kana_decoder::MaskedNgramScorer& scorer,
                   const kana_decoder::ConnectionModel& connections,
                   CaseResult& result, bool include_trace, bool include_lattice_trace) {
    result.input = input;
    const auto paths = kana_decoder::decode_reading(
        input.reading, lexicon, scorer, {}, &connections,
        include_trace ? &result.trace : nullptr,
        include_lattice_trace ? &result.lattice_trace : nullptr);

    if (!paths.empty()) {
        for (const auto& token : paths.front().tokens) {
            result.predicted += token.candidate.surface;
        }
    }
    result.decode_failure = paths.empty();
    result.edit_distance = levenshtein_distance(
        to_codepoints(result.predicted), to_codepoints(input.expected));
    return true;
}

int usage() {
    std::cerr << "usage: kana_decoder_eval PACK_DIR CORPUS_TSV [--details] [--trace] [--lattice-trace]\n";
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3 || argc > 5) return usage();
    bool details = false;
    bool trace = false;
    bool lattice_trace = false;
    for (int i = 3; i < argc; ++i) {
        const std::string option(argv[i]);
        if (option == "--details") details = true;
        else if (option == "--trace") { trace = true; details = true; }
        else if (option == "--lattice-trace") { lattice_trace = true; details = true; }
        else return usage();
    }

    kana_decoder::LexiconTrie lexicon;
    kana_decoder::MaskedNgramScorer scorer;
    kana_decoder::ConnectionModel connections;

    const auto load_started = std::chrono::steady_clock::now();
    if (!kana_decoder::load_language_pack(
            argv[1], lexicon, scorer, nullptr, &connections)) {
        std::cerr << "failed to load Language Pack\n";
        return 2;
    }
    const auto load_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - load_started).count();

    std::ifstream corpus(argv[2]);
    if (!corpus) {
        std::cerr << "cannot open evaluation corpus\n";
        return 2;
    }

    Stats overall;
    std::map<std::string, Stats> by_domain;
    std::vector<CaseResult> case_results;
    std::string line;
    std::size_t line_number = 0;

    while (std::getline(corpus, line)) {
        ++line_number;
        if (line.empty() || line[0] == '#') continue;

        EvaluationCase input;
        if (!split_tsv_row(line, input)) {
            std::cerr << "invalid corpus row or UTF-8 at line "
                      << line_number << "\n";
            return 2;
        }

        CaseResult result;
        evaluate_case(input, lexicon, scorer, connections, result, trace, lattice_trace);
        accumulate(overall, result);
        accumulate(by_domain[input.domain], result);
        if (details) case_results.push_back(std::move(result));
    }

    if (!corpus.eof()) {
        std::cerr << "failed while reading evaluation corpus\n";
        return 2;
    }
    if (overall.cases == 0) {
        std::cerr << "evaluation corpus is empty\n";
        return 2;
    }

    std::cout << std::fixed << std::setprecision(6)
              << "{\"pack_load_ms\":" << load_ms << ",\"overall\":";
    print_stats(overall);
    std::cout << ",\"domains\":{";
    bool first_domain = true;
    for (const auto& entry : by_domain) {
        if (!first_domain) std::cout << ',';
        first_domain = false;
        std::cout << "\"" << json_escape(entry.first) << "\":";
        print_stats(entry.second);
    }
    std::cout << '}';

    if (details) {
        std::cout << ",\"details\":[";
        for (std::size_t index = 0; index < case_results.size(); ++index) {
            if (index) std::cout << ',';
            print_case_result(case_results[index]);
        }
        std::cout << ']';
    }
    std::cout << "}\n";
    return 0;
}
