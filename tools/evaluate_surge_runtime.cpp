#include "kana_decoder/engine.hpp"
#include "kana_decoder/japanese_decoder.hpp"
#include "kana_decoder/japanese_language.hpp"
#include "kana_decoder/surge_runtime.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <limits>
#include <set>
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
bool load_decision_policy(const std::string& path,
    kana_decoder::SurgeRuntimeConfig& config) {
  std::ifstream input(path);
  if (!input) return false;
  std::string line;
  std::set<std::string> seen;
  std::size_t line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    if (line.empty() || line[0] == '#') continue;
    const auto fields = split_tabs(line);
    if (fields.size() != 2 || fields[0].empty() || !seen.insert(fields[0]).second)
      return false;
    try {
      std::size_t consumed = 0;
      const double value = std::stod(fields[1], &consumed);
      if (consumed != fields[1].size() || !std::isfinite(value) || value < 0.0)
        return false;
      if (fields[0] == "normalization_renderer")
        config.normalization_renderer_score = value;
      else
        config.recognition_family_weights[fields[0]] = value;
    } catch (...) {
      return false;
    }
  }
  return input.eof() && line_number > 0;
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
  if (argc < 3 || argc > 6) {
    std::cerr << "usage: kana_decoder_surge_eval PACK_DIR CORPUS_TSV [SPLIT=test] [TRAIN_SEQUENCES.tsv] [DECISION_POLICY.tsv]\n";
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
  if (argc >= 5) {
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

  kana_decoder::SurgeRuntimeConfig calibrated_policy;
  if (argc == 6 && !load_decision_policy(argv[5], calibrated_policy)) {
    std::cerr << "invalid decision policy file\\n";
    return 2;
  }
  std::size_t runtime_failures = 0;
  std::size_t exact_top1 = 0, exact_surge = 0, top1_distance = 0, surge_distance = 0;
  std::size_t candidate_exact_at_1 = 0, candidate_exact_at_5 = 0, candidate_exact_at_10 = 0;
  std::size_t surge_exact_when_candidate_available_at_10 = 0;
  std::size_t candidate_oracle_distance_at_5 = 0, candidate_oracle_distance_at_10 = 0;
  std::size_t candidate_paths_total = 0;
  std::size_t reference_points = 0, overrides = 0, deferred = 0, decode_failures = 0;
  kana_decoder::surge::JapaneseLanguageAdapter typed_entity_adapter;
  std::size_t input_points = 0;
  std::size_t typed_entities_found = 0, typed_entity_evidence_added = 0;
  std::size_t selected_typed_entity_evidence = 0, typed_entities_persisted = 0;
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
      candidate_oracle_distance_at_5 += missing;
      candidate_oracle_distance_at_10 += missing;
      continue;
    }
    std::string top1;
    for (const auto& token : paths.front().tokens) top1 += token.candidate.surface;
    const auto top1_ed = edit_distance(codepoints(top1), codepoints(item.expected));
    const auto expected_points = codepoints(item.expected);
    std::size_t oracle_distance_5 = std::numeric_limits<std::size_t>::max();
    std::size_t oracle_distance_10 = std::numeric_limits<std::size_t>::max();
    bool exact_5 = false, exact_10 = false;
    for (std::size_t rank = 0; rank < paths.size(); ++rank) {
      std::string candidate_surface;
      for (const auto& token : paths[rank].tokens) candidate_surface += token.candidate.surface;
      const auto distance = edit_distance(codepoints(candidate_surface), expected_points);
      if (rank == 0 && distance == 0) ++candidate_exact_at_1;
      if (rank < 5) { oracle_distance_5 = std::min(oracle_distance_5, distance); exact_5 = exact_5 || distance == 0; }
      if (rank < 10) { oracle_distance_10 = std::min(oracle_distance_10, distance); exact_10 = exact_10 || distance == 0; }
    }
    candidate_paths_total += paths.size();
    if (exact_5) ++candidate_exact_at_5;
    if (exact_10) ++candidate_exact_at_10;
    candidate_oracle_distance_at_5 += oracle_distance_5;
    candidate_oracle_distance_at_10 += oracle_distance_10;
    top1_distance += top1_ed;
    if (top1_ed == 0) ++exact_top1;

    const auto journal = (std::filesystem::temp_directory_path() /
        ("kana-decoder-surge-real-data-eval-" + std::to_string(case_index) + ".journal")).string();
    std::error_code ignored;
    std::filesystem::remove(journal, ignored);
    kana_decoder::SurgeRuntimeConfig config;
    config.journal_path = journal;
    config.context_id = "real-data-evaluation-" + std::to_string(case_index);
    config.max_journal_events = 4096;
    config.max_journal_transactions = 128;
    config.max_journal_payload_bytes = 1024U * 1024U;
    config.max_journal_identity_bytes = 512U * 1024U;
    config.remove_journal_on_destroy = true;
    config.language_adapter = &typed_entity_adapter;
    config.recognition_family_weights = calibrated_policy.recognition_family_weights;
    config.normalization_renderer_score = calibrated_policy.normalization_renderer_score;
    kana_decoder::SurgeNormalizationRuntime runtime(config);
    std::string runtime_error;
    if (!runtime.open(runtime_error)) {
      ++runtime_failures;
      ++deferred;
      surge_distance += codepoints(item.expected).size();
      continue;
    }
    const auto normalized = runtime.normalize(item.reading, paths, true);
    typed_entities_found += normalized.metrics.typed_entities_found;
    typed_entity_evidence_added += normalized.metrics.typed_entity_evidence_added;
    selected_typed_entity_evidence += normalized.metrics.selected_typed_entity_evidence;
    typed_entities_persisted += normalized.metrics.typed_entities_persisted;
    if (!normalized.committed) {
      ++deferred;
      surge_distance += codepoints(item.expected).size();
      continue;
    }
    const auto surge_ed = edit_distance(codepoints(normalized.output), codepoints(item.expected));
    surge_distance += surge_ed;
    if (surge_ed == 0) {
      ++exact_surge;
      if (exact_10) ++surge_exact_when_candidate_available_at_10;
    }
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
      << ",\"training_sequences_used\":" << (argc >= 5 ? "true" : "false")
      << ",\"decision_policy_used\":" << (argc == 6 ? "true" : "false")
      << ",\"decision_policy\":{\"normalization_renderer\":"
      << calibrated_policy.normalization_renderer_score << ",\"family_weights\":{";
  {
    bool first = true;
    for (const auto& item : calibrated_policy.recognition_family_weights) {
      if (!first) std::cout << ",";
      first = false;
      std::cout << "\"" << item.first << "\":" << item.second;
    }
  }
  std::cout << "}}"
      << ",\"cases\":" << cases.size()
      << ",\"input_codepoints\":" << input_points
      << ",\"reference_codepoints\":" << reference_points
      << ",\"candidate_generation\":{\"mean_paths\":" << (cases.empty() ? 0.0 : static_cast<double>(candidate_paths_total) / cases.size())
      << ",\"surface_exact_recall_at_1\":" << (cases.empty() ? 0.0 : static_cast<double>(candidate_exact_at_1) / cases.size())
      << ",\"surface_exact_recall_at_5\":" << (cases.empty() ? 0.0 : static_cast<double>(candidate_exact_at_5) / cases.size())
      << ",\"surface_exact_recall_at_10\":" << (cases.empty() ? 0.0 : static_cast<double>(candidate_exact_at_10) / cases.size())
      << ",\"surge_exact_given_candidate_at_10\":" << (candidate_exact_at_10 ? static_cast<double>(surge_exact_when_candidate_available_at_10) / candidate_exact_at_10 : 0.0)
      << ",\"oracle_cer_at_5\":" << (reference_points ? static_cast<double>(candidate_oracle_distance_at_5) / reference_points : 0.0)
      << ",\"oracle_cer_at_10\":" << (reference_points ? static_cast<double>(candidate_oracle_distance_at_10) / reference_points : 0.0) << "}"
      << ",\"decoder_top1\":{\"cer\":" << cer_top1
      << ",\"sentence_exact\":" << (cases.empty() ? 0.0 : static_cast<double>(exact_top1) / cases.size()) << "}"
      << ",\"surge_core\":{\"cer\":" << cer_surge
      << ",\"sentence_exact\":" << (cases.empty() ? 0.0 : static_cast<double>(exact_surge) / cases.size())
      << ",\"overrides\":" << overrides
      << ",\"deferred\":" << deferred
      << ",\"runtime_failures\":" << runtime_failures
      << ",\"decode_failures\":" << decode_failures
      << ",\"typed_entities_found\":" << typed_entities_found
      << ",\"typed_entity_evidence_added\":" << typed_entity_evidence_added
      << ",\"selected_typed_entity_evidence\":" << selected_typed_entity_evidence
      << ",\"typed_entities_persisted\":" << typed_entities_persisted << "}"
      << ",\"elapsed_seconds\":" << elapsed
      << ",\"sentences_per_second\":" << (elapsed > 0.0 ? cases.size() / elapsed : 0.0)
      << ",\"seconds_per_1000_input_codepoints\":"
      << (input_points ? elapsed * 1000.0 / input_points : 0.0)
      << ",\"peak_rss_kib\":" << peak_rss_kib() << "}\n";
  return 0;
}
