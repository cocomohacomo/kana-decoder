#include "kana_decoder/surge_runtime.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using Nanoseconds = std::chrono::nanoseconds;

struct Sample {
  long long total_ns{0};
  long long ns_per_normalize{0};
  std::size_t graph_nodes{0};
  std::uint64_t journal_state_version{0};
};

long long median(std::vector<long long> values) {
  std::sort(values.begin(), values.end());
  const auto middle = values.size() / 2;
  if (values.size() % 2 != 0) return values[middle];
  return values[middle - 1] + (values[middle] - values[middle - 1]) / 2;
}

bool run_sample(const std::string& label, const std::string& suffix,
                const std::vector<kana_decoder::DecodedPath>& paths,
                int warmup_iterations, int iterations, Sample& sample) {
  const auto journal = (std::filesystem::temp_directory_path() /
      ("kana-decoder-surge-benchmark-" + label + "-" + suffix + ".journal")).string();
  kana_decoder::SurgeRuntimeConfig config;
  config.journal_path = journal;
  config.context_id = "surge-runtime-benchmark-" + label + "-" + suffix;
  config.remove_journal_on_destroy = true;
  kana_decoder::SurgeNormalizationRuntime runtime(config);
  std::string error;
  if (!runtime.open(error)) {
    std::cerr << error << "\\n";
    return false;
  }

  for (int i = 0; i < warmup_iterations; ++i) {
    const auto result = runtime.normalize("さんじゅうに", paths, true);
    if (!result.committed) {
      std::cerr << result.error << "\\n";
      return false;
    }
  }

  const auto begin = Clock::now();
  for (int i = 0; i < iterations; ++i) {
    const auto result = runtime.normalize("さんじゅうに", paths, true);
    if (!result.committed) {
      std::cerr << result.error << "\\n";
      return false;
    }
  }
  sample.total_ns =
      std::chrono::duration_cast<Nanoseconds>(Clock::now() - begin).count();
  sample.ns_per_normalize = sample.total_ns / iterations;
  sample.graph_nodes = runtime.graph_node_count();
  sample.journal_state_version = runtime.state_version();
  return true;
}

bool run_case(const std::string& label,
              const std::vector<kana_decoder::DecodedPath>& paths,
              int warmup_iterations, int iterations, int repeats) {
  std::vector<long long> totals;
  std::vector<long long> per_operations;
  totals.reserve(static_cast<std::size_t>(repeats));
  per_operations.reserve(static_cast<std::size_t>(repeats));

  Sample last_sample;
  for (int repeat = 0; repeat < repeats; ++repeat) {
    Sample sample;
    if (!run_sample(label, std::to_string(repeat), paths, warmup_iterations,
                    iterations, sample)) {
      return false;
    }
    totals.push_back(sample.total_ns);
    per_operations.push_back(sample.ns_per_normalize);
    last_sample = sample;
  }

  const auto min_value = *std::min_element(per_operations.begin(), per_operations.end());
  const auto max_value = *std::max_element(per_operations.begin(), per_operations.end());
  std::cout << "surge_runtime_mode=" << label
            << " iterations=" << iterations
            << " warmup_iterations=" << warmup_iterations
            << " repeats=" << repeats
            << " median_total_ns=" << median(totals)
            << " median_ns_per_normalize=" << median(per_operations)
            << " min_ns_per_normalize=" << min_value
            << " max_ns_per_normalize=" << max_value
            << " graph_nodes=" << last_sample.graph_nodes
            << " journal_state_version=" << last_sample.journal_state_version
            << "\\n";
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  const int iterations = argc > 1 ? std::max(1, std::stoi(argv[1])) : 50;
  const int repeats = argc > 2 ? std::max(3, std::stoi(argv[2])) : 5;
  const int warmup_iterations = std::max(5, std::min(20, iterations / 5));

  kana_decoder::DecodedPath first;
  first.score = -10.0;
  kana_decoder::DecodedToken token;
  token.start = 0;
  token.end = 6;
  token.candidate.id = "number-kanji";
  token.candidate.surface = "三十二";
  token.candidate.morphology.language_tag = "ja-JP";
  token.candidate.morphology.part_of_speech = "名詞,数";
  token.lexical_score = -3.0;
  token.transition_score = -0.5;
  token.ngram_score = -0.7;
  token.cluster_raw_evidence = -0.2;
  token.cluster_score = 0.2;
  token.pos_score = -0.1;
  token.has_transition_evidence = true;
  token.has_ngram_evidence = true;
  token.has_cluster_evidence = true;
  token.has_pos_evidence = true;
  first.tokens.push_back(token);

  kana_decoder::DecodedPath second;
  second.score = -11.0;
  token.candidate.id = "number-kana";
  token.candidate.surface = "さんじゅうに";
  second.tokens.push_back(token);

  if (!run_case("single", {first}, warmup_iterations, iterations, repeats)) return 3;
  if (!run_case("ambiguous", {first, second}, warmup_iterations, iterations, repeats)) return 4;
  return 0;
}
