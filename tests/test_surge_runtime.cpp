#include "kana_decoder/surge_runtime.hpp"

#include <cassert>
#include <filesystem>
#include <string>

int main() {
  const auto journal = (std::filesystem::temp_directory_path() /
      "kana-decoder-surge-runtime-test.journal").string();
  std::error_code ignored;
  std::filesystem::remove(journal, ignored);

  kana_decoder::SurgeRuntimeConfig config;
  config.journal_path = journal;
  config.context_id = "runtime-conformance-test";
  config.remove_journal_on_destroy = true;
  kana_decoder::SurgeNormalizationRuntime runtime(config);
  std::string error;
  assert(runtime.open(error));
  assert(runtime.graph_node_count() == 1);

  kana_decoder::DecodedPath kanji;
  kanji.score = -10.0;
  kana_decoder::DecodedToken kanji_token;
  kanji_token.start = 0;
  kanji_token.end = 6;
  kanji_token.candidate.id = "number-kanji";
  kanji_token.candidate.surface = "三十二";
  kanji_token.candidate.pos = "名詞,数";
  kanji_token.morphology_score = -5.0;
  kanji_token.context_score = -5.0;
  kanji.tokens.push_back(kanji_token);

  kana_decoder::DecodedPath kana;
  kana.score = -11.0;
  kana_decoder::DecodedToken kana_token = kanji_token;
  kana_token.candidate.id = "number-kana";
  kana_token.candidate.surface = "さんじゅうに";
  kana.tokens.push_back(kana_token);

  const auto result = runtime.normalize("さんじゅうに", {kanji, kana});
  assert(result.committed);
  assert(result.output == "32");
  assert(result.metrics.claims == 2);
  assert(result.metrics.evidence == 2);
  assert(result.metrics.entities == 2);
  assert(result.metrics.contradictions == 1);
  assert(result.metrics.questions == 1);
  assert(result.metrics.chaos_members == 2);
  assert(result.metrics.state_version > 0);
  assert(runtime.graph_node_count() >= 10);

  std::string commit_error;
  assert(runtime.commit_output("32", 7, true, commit_error));
  assert(runtime.state_version() > result.metrics.state_version);

  return 0;
}
