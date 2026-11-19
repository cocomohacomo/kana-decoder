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
#include <sys/resource.h>
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
long peak_rss_kib() {
  struct rusage usage {};
  return getrusage(RUSAGE_SELF, &usage) == 0 ? usage.ru_maxrss : -1;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc < 3 || argc > 4) {
    std::cerr << "usage: kana_decoder_surge_eval PACK_DIR CORPUS_TSV [SPLIT=test]\n";
    return 2;
  }
  kana_decoder::LexiconTrie lexicon;
  kana_decoder::MaskedNgramScorer scorer;
  kana_decoder::ConnectionModel connections;
  if (!kana_decoder::load_language_pack(argv[1], lexicon, scorer, nullptr, &connections)) {
    std::cerr << "failed to load language pack\n";
    return 2;
  }
  std::vector<Case> cases;
  const std::string split = argc == 4 ? argv[3] : "test";
  if (!load_cases(argv[2], split, cases)) {
    std::cerr << "failed to load evaluation corpus or unsupported TSV format\n";
    return 2;
  }

  const auto journal = (std::filesystem::temp_directory_path() /
      "kana-decoder-surge-real-data-eval.journal").string();
  std::error_code ignored;
  std::filesystem::remove(journal, ignored);
  kana_decoder::SurgeRuntimeConfig config;
  config.journal_path = journal;
  config.context_id = "real-data-evaluation";
  config.max_journal_events = std::max<std::size_t>(65536, cases.size() * 128);
  config.max_journal_transactions = std::max<std::size_t>(8192, cases.size() * 32);
  config.max_journal_payload_bytes = 64U * 1024U * 1024U;
  config.max_journal_identity_bytes = 32U * 1024U * 1024U;
  config.remove_journal_on_destroy = true;
  kana_decoder::SurgeNormalizationRuntime runtime(config);
  std::string error;
  if (!runtime.open(error)) {
    std::cerr << "failed to open SURGE runtime: " << error << "\n";
    return 2;
  }

  std::size_t exact_top1 = 0, exact_surge = 0, top1_distance = 0, surge_distance = 0;
  std::size_t reference_points = 0, overrides = 0, deferred = 0, decode_failures = 0;
  std::size_t input_points = 0;
  const auto started = std::chrono::steady_clock::now();
  for (const auto& item : cases) {
    input_points += codepoints(item.reading).size();
    reference_points += codepoints(item.expected).size();
    const auto paths = kana_decoder::decode_reading(item.reading, lexicon, scorer, {},
        &connections);
    if (paths.empty()) {
      ++decode_failures;
      continue;
    }
    std::string top1;
    for (const auto& token : paths.front().tokens) top1 += token.candidate.surface;
    const auto top1_ed = edit_distance(codepoints(top1), codepoints(item.expected));
    top1_distance += top1_ed;
    if (top1_ed == 0) ++exact_top1;

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
      << ",\"cases\":" << cases.size()
      << ",\"input_codepoints\":" << input_points
      << ",\"reference_codepoints\":" << reference_points
      << ",\"decoder_top1\":{\"cer\":" << cer_top1
      << ",\"sentence_exact\":" << static_cast<double>(exact_top1) / cases.size() << "}"
      << ",\"surge_core\":{\"cer\":" << cer_surge
      << ",\"sentence_exact\":" << static_cast<double>(exact_surge) / cases.size()
      << ",\"overrides\":" << overrides
      << ",\"deferred\":" << deferred
      << ",\"decode_failures\":" << decode_failures << "}"
      << ",\"elapsed_seconds\":" << elapsed
      << ",\"sentences_per_second\":" << (elapsed > 0.0 ? cases.size() / elapsed : 0.0)
      << ",\"seconds_per_1000_input_codepoints\":"
      << (input_points ? elapsed * 1000.0 / input_points : 0.0)
      << ",\"peak_rss_kib\":" << peak_rss_kib() << "}\n";
  return 0;
}
