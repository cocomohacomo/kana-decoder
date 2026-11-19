#include "kana_decoder/engine.hpp"
#include "kana_decoder/surge_runtime.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>
#if !defined(_WIN32)
#include <sys/resource.h>
#endif
#include <vector>

namespace {
struct Case { std::string reading, expected; };
std::vector<std::string> split_tabs(const std::string& line) {
  std::vector<std::string> out;
  std::size_t begin = 0;
  for (;;) {
    const auto end = line.find('\t', begin);
    out.push_back(line.substr(begin, end == std::string::npos ? std::string::npos : end - begin));
    if (end == std::string::npos) break;
    begin = end + 1;
  }
  return out;
}
std::vector<std::string> codepoints(const std::string& value) {
  std::vector<std::string> out;
  if (!kana_decoder::is_valid_utf8(value)) return out;
  for (std::size_t i = 0; i < value.size();) {
    const auto lead = static_cast<unsigned char>(value[i]);
    const std::size_t width = lead < 0x80 ? 1 : (lead & 0xe0) == 0xc0 ? 2 :
        (lead & 0xf0) == 0xe0 ? 3 : 4;
    out.push_back(value.substr(i, width));
    i += width;
  }
  return out;
}
std::size_t edit_distance(const std::vector<std::string>& a,
                          const std::vector<std::string>& b) {
  std::vector<std::size_t> prev(b.size() + 1), cur(b.size() + 1);
  for (std::size_t j = 0; j <= b.size(); ++j) prev[j] = j;
  for (std::size_t i = 1; i <= a.size(); ++i) {
    cur[0] = i;
    for (std::size_t j = 1; j <= b.size(); ++j)
      cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1,
          prev[j - 1] + (a[i - 1] == b[j - 1] ? 0U : 1U)});
    prev.swap(cur);
  }
  return prev.back();
}
void add_sequence_counts(const std::vector<std::string>& tokens,
                        kana_decoder::MaskedNgramScorer& scorer) {
  for (std::size_t i = 0; i < tokens.size(); ++i) {
    const auto& token = tokens[i];
    if (token.empty()) continue;
    scorer.add_unigram(token, 1);
    if (i >= 1 && !tokens[i - 1].empty())
      scorer.add_left_bigram(tokens[i - 1], token, 1);
    if (i >= 2 && !tokens[i - 2].empty() && !tokens[i - 1].empty()) {
      scorer.add_left_ngram({tokens[i - 2], tokens[i - 1]}, token, 1);
      scorer.add_left_skipgram(tokens[i - 2], token, 1);
    }
    if (i >= 3 && !tokens[i - 3].empty() && !tokens[i - 2].empty() &&
        !tokens[i - 1].empty())
      scorer.add_left_ngram({tokens[i - 3], tokens[i - 2], tokens[i - 1]}, token, 1);
  }
}
bool load_training_sequences(const std::string& path,
    const kana_decoder::LexiconTrie& lexicon,
    kana_decoder::MaskedNgramScorer& scorer,
    kana_decoder::MaskedNgramScorer& pos_scorer) {
  std::ifstream input(path);
  if (!input) return false;
  std::string line;
  while (std::getline(input, line)) {
    if (line.empty() || line[0] == '#') continue;
    const auto tokens = split_tabs(line);
    add_sequence_counts(tokens, scorer);
    std::vector<std::string> pos_tokens;
    pos_tokens.reserve(tokens.size());
    for (const auto& token : tokens) pos_tokens.push_back(lexicon.candidate_pos(token));
    add_sequence_counts(pos_tokens, pos_scorer);
  }
  return input.eof();
}
bool load_cases(const std::string& path, const std::string& split,
                std::vector<Case>& cases) {
  std::ifstream input(path);
  if (!input) return false;
  std::map<std::pair<std::string, std::string>, std::size_t> index;
  std::string line;
  while (std::getline(input, line)) {
    if (line.empty() || line[0] == '#') continue;
    const auto fields = split_tabs(line);
    if (fields.size() == 3) {
      if (fields[0].empty() || fields[1].empty() ||
          !kana_decoder::is_valid_utf8(fields[0]) ||
          !kana_decoder::is_valid_utf8(fields[1])) return false;
      cases.push_back({fields[0], fields[1]});
    } else if (fields.size() == 6) {
      if (fields[5] != split) continue;
      if (fields[0].empty() || fields[1].empty() || fields[2].empty() ||
          fields[3].empty() || !kana_decoder::is_valid_utf8(fields[2]) ||
          !kana_decoder::is_valid_utf8(fields[3])) return false;
      const auto key = std::make_pair(fields[0], fields[1]);
      auto found = index.find(key);
      if (found == index.end()) {
        const auto next = cases.size();
        index.emplace(key, next);
        cases.push_back({});
        found = index.find(key);
      }
      cases[found->second].reading += fields[2];
      cases[found->second].expected += fields[3];
    } else {
      return false;
    }
  }
  return input.eof() && !cases.empty();
}
#if defined(_WIN32)
long peak_rss_kib() { return -1; }
#else
long peak_rss_kib() {
  struct rusage usage {};
  return getrusage(RUSAGE_SELF, &usage) == 0 ? usage.ru_maxrss : -1;
}
#endif
}  // namespace

int main(int argc, char** argv) {
  if (argc < 3 || argc > 5) {
    std::cerr << "usage: kana_decoder_surge_eval PACK_DIR CORPUS_TSV [SPLIT=test] [TRAIN_SEQUENCES.tsv]\n";
    return 2;
  }
  kana_decoder::LexiconTrie lexicon;
  kana_decoder::MaskedNgramScorer scorer;
  kana_decoder::ConnectionModel connections;
  if (!kana_decoder::load_language_pack(argv[1], lexicon, scorer, nullptr, &connections)) {
    std::cerr << "failed to load language pack\n";
    return 2;
  }
  kana_decoder::MaskedNgramScorer* active_scorer = &scorer;
  kana_decoder::MaskedNgramScorer trained_scorer, trained_pos_scorer;
  kana_decoder::DecoderOptions decoder_options;
  if (argc == 5) {
    lexicon.build_candidate_pos_index();
    if (!load_training_sequences(argv[4], lexicon, trained_scorer, trained_pos_scorer)) {
      std::cerr << "failed to load training sequences\n";
      return 2;
    }
    active_scorer = &trained_scorer;
    decoder_options.pos_ngram_scorer = &trained_pos_scorer;
    decoder_options.pos_context_weight = 0.15;
    decoder_options.surge_left_context_gate = true;
  }
  std::vector<Case> cases;
  const std::string split = argc >= 4 ? argv[3] : "test";
  if (!load_cases(argv[2], split, cases)) {
    std::cerr << "failed to load evaluation corpus or unsupported TSV format\n";
    return 2;
  }

  std::size_t runtime_failures = 0;
  std::size_t exact_top1 = 0, exact_surge = 0, top1_distance = 0, surge_distance = 0;
  std::size_t reference_points = 0, overrides = 0, deferred = 0, decode_failures = 0;
  std::size_t input_points = 0;
  const auto started = std::chrono::steady_clock::now();
  for (std::size_t case_index = 0; case_index < cases.size(); ++case_index) {
    const auto& item = cases[case_index];
    input_points += codepoints(item.reading).size();
    reference_points += codepoints(item.expected).size();
    const auto paths = kana_decoder::decode_reading(item.reading, lexicon, *active_scorer, decoder_options,
        &connections);
    if (paths.empty()) {
      ++decode_failures;
      const auto missing = codepoints(item.expected).size();
      top1_distance += missing;
      surge_distance += missing;
      continue;
    }
    std::string top1;
    for (const auto& token : paths.front().tokens) top1 += token.candidate.surface;
    const auto top1_ed = edit_distance(codepoints(top1), codepoints(item.expected));
    top1_distance += top1_ed;
    if (top1_ed == 0) ++exact_top1;

    const auto journal = (std::filesystem::temp_directory_path() /
        ("kana-decoder-surge-real-data-eval-" + std::to_string(case_index) + ".journal")).string();
    kana_decoder::SurgeRuntimeConfig config;
    config.journal_path = journal;
    config.context_id = "real-data-evaluation-" + std::to_string(case_index);
    config.max_journal_events = 512;
    config.max_journal_transactions = 128;
    config.max_journal_payload_bytes = 1024U * 1024U;
    config.max_journal_identity_bytes = 512U * 1024U;
    config.remove_journal_on_destroy = true;
    kana_decoder::SurgeNormalizationRuntime runtime(config);
    std::string runtime_error;
    if (!runtime.open(runtime_error)) {
      ++runtime_failures;
      ++deferred;
      surge_distance += codepoints(item.expected).size();
      continue;
    }
    const auto normalized = runtime.normalize(item.reading, paths, true);
    if (!normalized.committed) {
      ++deferred;
      surge_distance += codepoints(item.expected).size();
      continue;
    }
    const auto surge_ed = edit_distance(codepoints(normalized.output), codepoints(item.expected));
    surge_distance += surge_ed;
    if (surge_ed == 0) ++exact_surge;
    if (normalized.output != top1) ++overrides;
  }
  const auto elapsed = std::chrono::duration<double>(
      std::chrono::steady_clock::now() - started).count();
  const double cer_top1 = reference_points
      ? static_cast<double>(top1_distance) / reference_points : 0.0;
  const double cer_surge = reference_points
      ? static_cast<double>(surge_distance) / reference_points : 0.0;
  std::cout << std::fixed << std::setprecision(6)
      << "{\"mode\":\"surge_core_real_data_evaluation\""
      << ",\"split\":\"" << split << "\""
      << ",\"training_sequences_used\":" << (argc == 5 ? "true" : "false")
      << ",\"cases\":" << cases.size()
      << ",\"input_codepoints\":" << input_points
      << ",\"reference_codepoints\":" << reference_points
      << ",\"decoder_top1\":{\"cer\":" << cer_top1
      << ",\"sentence_exact\":" << static_cast<double>(exact_top1) / cases.size() << "}"
      << ",\"surge_core\":{\"cer\":" << cer_surge
      << ",\"sentence_exact\":" << static_cast<double>(exact_surge) / cases.size()
      << ",\"overrides\":" << overrides
      << ",\"deferred\":" << deferred
      << ",\"runtime_failures\":" << runtime_failures
      << ",\"decode_failures\":" << decode_failures << "}"
      << ",\"elapsed_seconds\":" << elapsed
      << ",\"sentences_per_second\":" << (elapsed > 0.0 ? cases.size() / elapsed : 0.0)
      << ",\"seconds_per_1000_input_codepoints\":"
      << (input_points ? elapsed * 1000.0 / input_points : 0.0)
      << ",\"peak_rss_kib\":" << peak_rss_kib() << "}\n";
  return 0;
}
