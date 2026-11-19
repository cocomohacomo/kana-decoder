#include "kana_decoder/surge_runtime.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
  const int iterations = argc > 1 ? std::max(1, std::stoi(argv[1])) : 50;
  const auto journal = (std::filesystem::temp_directory_path() /
      "kana-decoder-surge-benchmark.journal").string();
  kana_decoder::SurgeRuntimeConfig config;
  config.journal_path = journal;
  config.context_id = "surge-runtime-benchmark";
  config.remove_journal_on_destroy = true;
  kana_decoder::SurgeNormalizationRuntime runtime(config);
  std::string error;
  if (!runtime.open(error)) {
    std::cerr << error << "\n";
    return 2;
  }

  kana_decoder::DecodedPath first;
  first.score = -10.0;
  kana_decoder::DecodedToken token;
  token.start = 0;
  token.end = 6;
  token.candidate.id = "number-kanji";
  token.candidate.surface = "三十二";
  token.candidate.pos = "名詞,数";
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
  const std::vector<kana_decoder::DecodedPath> paths{first, second};

  const auto begin = std::chrono::steady_clock::now();
  for (int i = 0; i < iterations; ++i) {
    const auto result = runtime.normalize("さんじゅうに", paths, true);
    if (!result.committed) {
      std::cerr << result.error << "\n";
      return 3;
    }
  }
  const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - begin).count();
  std::cout << "surge_runtime_iterations=" << iterations
            << " total_ns=" << elapsed
            << " ns_per_normalize=" << (elapsed / iterations)
            << " graph_nodes=" << runtime.graph_node_count()
            << " journal_state_version=" << runtime.state_version() << "\n";
  return 0;
}
