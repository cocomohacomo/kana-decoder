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
  config.remove_journal_on_destroy = false;
  std::uint64_t committed_version = 0;
  std::size_t committed_nodes = 0;
  {
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
  kanji_token.lexical_score = -4.0;
  kanji_token.transition_score = -1.0;
  kanji_token.ngram_score = -0.7;
  kanji_token.cluster_score = 0.4;
  kanji_token.cluster_raw_evidence = -0.2;
  kanji_token.pos_score = -0.1;
  kanji_token.has_transition_evidence = true;
  kanji_token.has_ngram_evidence = true;
  kanji_token.has_cluster_evidence = true;
  kanji_token.has_pos_evidence = true;
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
  assert(result.metrics.evidence == 10);
  assert(result.metrics.entities == 2);
  assert(result.metrics.contradictions == 1);
  assert(result.metrics.questions == 1);
  assert(result.metrics.chaos_members == 1);
  assert(result.metrics.state_version > 0);
  assert(result.selected_claim_ids.size() == 1);
  assert(runtime.graph_node_count() >= 10);

  ::surge::TemporalReassessmentRecord temporal_record;
  std::string temporal_error;
  assert(runtime.reassess_claim(result.selected_claim_ids.front(),
      "9999-01-01T00:00:00Z", temporal_record, temporal_error));
  assert(temporal_record.claim_temporal_state == ::surge::ClaimTemporalState::Valid);

  std::string commit_error;
  assert(runtime.commit_output("32", 7, true, commit_error));
  assert(runtime.state_version() > result.metrics.state_version);
  committed_version = runtime.state_version();
  committed_nodes = runtime.graph_node_count();
  }

  config.remove_journal_on_destroy = true;
  {
    kana_decoder::SurgeNormalizationRuntime recovered(config);
    std::string recovery_error;
    assert(recovered.open(recovery_error));
    assert(recovered.state_version() == committed_version);
    assert(recovered.graph_node_count() == committed_nodes);
  }

  // SURGE Core must be able to overturn the decoder's first-ranked path.
  const auto decision_journal = (std::filesystem::temp_directory_path() /
      "kana-decoder-surge-core-decision-test.journal").string();
  std::filesystem::remove(decision_journal, ignored);
  kana_decoder::SurgeRuntimeConfig decision_config;
  decision_config.journal_path = decision_journal;
  decision_config.context_id = "core-path-reselection-test";
  decision_config.remove_journal_on_destroy = true;
  {
    kana_decoder::SurgeNormalizationRuntime decision_runtime(decision_config);
    std::string decision_error;
    assert(decision_runtime.open(decision_error));
    kana_decoder::DecodedPath decoder_first;
    decoder_first.score = -1.0;
    kana_decoder::DecodedToken first_token;
    first_token.start = 0;
    first_token.end = 5;
    first_token.candidate.id = "decoder-top";
    first_token.candidate.surface = "交渉";
    first_token.candidate.pos = "名詞";
    first_token.lexical_score = -1.0;
    first_token.morphology_score = -1.0;
    first_token.cluster_raw_evidence = -10.0;
    first_token.has_cluster_evidence = true;
    decoder_first.tokens.push_back(first_token);

    kana_decoder::DecodedPath core_preferred;
    core_preferred.score = -2.0;
    kana_decoder::DecodedToken second_token = first_token;
    second_token.candidate.id = "core-preferred";
    second_token.candidate.surface = "高尚";
    second_token.cluster_raw_evidence = 10.0;
    core_preferred.tokens.push_back(second_token);

    const auto selected = decision_runtime.normalize("こうしょう",
        {decoder_first, core_preferred}, true);
    assert(selected.committed);
    assert(selected.output == "高尚");
    assert(selected.selected_claim_ids.size() == 1);
  }

  return 0;
}
