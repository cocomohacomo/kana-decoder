#include "kana_decoder/surge_runtime.hpp"
#include "kana_decoder/japanese_language.hpp"

#include <cassert>
#include <filesystem>
#include <string>
#include <iostream>

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
  if (!result.committed) std::cerr << "initial normalize failed: " << result.error << '\n';
  assert(result.committed);
  assert(result.output == "32");
  assert(result.metrics.claims == 2);
  assert(result.metrics.evidence == 10);
  assert(result.metrics.entities == 2);
  assert(result.metrics.clusters >= 3); // root + interpretation + surface
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

    // A later packet reuses the active Focus cluster. The previously retained
    // surface must outrank an otherwise tied unseen candidate, and the
    // historical dependency must be re-evaluated under its validity record.
    kana_decoder::DecodedPath unseen_path;
    unseen_path.score = -1.0;
    auto unseen_token = first_token;
    unseen_token.candidate.id = "unseen-candidate";
    unseen_token.candidate.surface = "公証";
    unseen_token.lexical_score = -1.0;
    unseen_token.morphology_score = -1.0;
    unseen_token.cluster_raw_evidence = 0.0;
    unseen_path.tokens.push_back(unseen_token);

    kana_decoder::DecodedPath focused_path;
    focused_path.score = -1.0;
    auto focused_token = second_token;
    focused_token.lexical_score = -1.0;
    focused_token.morphology_score = -1.0;
    focused_token.cluster_raw_evidence = 0.0;
    focused_path.tokens.push_back(focused_token);

    const auto recalled = decision_runtime.normalize("こうしょう",
        {unseen_path, focused_path}, true);
    assert(recalled.committed);
    assert(recalled.output == "高尚");
    assert(recalled.metrics.focus_retrieved_claims >= 2);
    assert(recalled.metrics.focus_evidence_added >= 1);
    assert(recalled.metrics.dependency_triggered >= 1);
    assert(recalled.metrics.dependency_impacted_nodes >= 2);
    assert(recalled.metrics.retrieval_relations_scanned > 0);
  }

  // The selected path is rendered only after SURGE has selected its
  // interpretation and token-level surface. The Japanese provider then adds
  // policy-controlled sentence punctuation.
  const auto renderer_journal = (std::filesystem::temp_directory_path() /
      "kana-decoder-surge-renderer-test.journal").string();
  std::filesystem::remove(renderer_journal, ignored);
  kana_decoder::surge::JapaneseLanguageAdapter japanese_adapter;
  kana_decoder::SurgeRuntimeConfig renderer_config;
  renderer_config.journal_path = renderer_journal;
  renderer_config.context_id = "language-renderer-integration-test";
  renderer_config.language_adapter = &japanese_adapter;
  renderer_config.normalization_renderer_score = 100.0;
  renderer_config.surface_render_options.append_terminal_punctuation = true;
  renderer_config.remove_journal_on_destroy = true;
  {
    kana_decoder::SurgeNormalizationRuntime renderer_runtime(renderer_config);
    std::string renderer_error;
    assert(renderer_runtime.open(renderer_error));
    kana_decoder::DecodedPath numeric_path;
    kana_decoder::DecodedToken numeric_token;
    numeric_token.start = 0;
    numeric_token.end = 6;
    numeric_token.candidate.id = "numeric-reading";
    numeric_token.candidate.surface = "三十二";
    numeric_token.candidate.pos = "名詞,数";
    numeric_path.tokens.push_back(numeric_token);
    const auto rendered = renderer_runtime.normalize("さんじゅうに", {numeric_path}, true);
    assert(rendered.committed);
    assert(rendered.output == "32。");
  }

  // Family calibration must be injected into the SURGE Core decision itself.
  const auto calibrated_journal = (std::filesystem::temp_directory_path() /
      "kana-decoder-surge-core-calibrated-test.journal").string();
  std::filesystem::remove(calibrated_journal, ignored);
  kana_decoder::SurgeRuntimeConfig calibrated_config;
  calibrated_config.journal_path = calibrated_journal;
  calibrated_config.context_id = "core-family-calibration-test";
  calibrated_config.recognition_family_weights["context_cluster"] = 0.0;
  calibrated_config.remove_journal_on_destroy = true;
  {
    kana_decoder::SurgeNormalizationRuntime calibrated_runtime(calibrated_config);
    std::string calibrated_error;
    assert(calibrated_runtime.open(calibrated_error));
    kana_decoder::DecodedPath calibrated_decoder_first;
    calibrated_decoder_first.score = -1.0;
    kana_decoder::DecodedToken calibrated_first_token;
    calibrated_first_token.start = 0;
    calibrated_first_token.end = 5;
    calibrated_first_token.candidate.id = "decoder-top";
    calibrated_first_token.candidate.surface = "交渉";
    calibrated_first_token.candidate.pos = "名詞";
    calibrated_first_token.lexical_score = -1.0;
    calibrated_first_token.morphology_score = -1.0;
    calibrated_first_token.cluster_raw_evidence = -10.0;
    calibrated_first_token.has_cluster_evidence = true;
    calibrated_decoder_first.tokens.push_back(calibrated_first_token);
    kana_decoder::DecodedPath calibrated_core_preferred;
    calibrated_core_preferred.score = -2.0;
    auto calibrated_second_token = calibrated_first_token;
    calibrated_second_token.candidate.id = "core-preferred";
    calibrated_second_token.candidate.surface = "高尚";
    calibrated_second_token.cluster_raw_evidence = 10.0;
    calibrated_core_preferred.tokens.push_back(calibrated_second_token);
    const auto calibrated = calibrated_runtime.normalize("こうしょう",
        {calibrated_decoder_first, calibrated_core_preferred}, true);
    assert(calibrated.committed);
    assert(calibrated.output == "交渉");
  }

  return 0;
}
