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

  kana_decoder::surge::JapaneseLanguageAdapter numeric_adapter;
  kana_decoder::SurgeRuntimeConfig config;
  config.language_adapter = &numeric_adapter;
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
  kanji_token.candidate.morphology.language_tag = "ja-JP";
  kanji_token.candidate.morphology.part_of_speech = "名詞,数";
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

  // Observation exposes the actual graph state without returning a selected
  // surface or changing the Core's state version.
  const auto observation_version = runtime.state_version();
  const auto observation = runtime.observe();
  assert(observation.available);
  assert(observation.context_id == "runtime-conformance-test");
  assert(observation.state_version == observation_version);
  assert(observation.total_nodes == runtime.graph_node_count());
  assert(!observation.nodes.empty());
  assert(!observation.relations.empty());
  bool observed_claim = false;
  for (const auto& node : observation.nodes)
    if (node.kind == ::surge::NodeKind::Claim &&
        node.content.find("candidate interpretation:") == 0)
      observed_claim = true;
  assert(observed_claim);
  const auto bounded_observation = runtime.observe(1, 1);
  assert(bounded_observation.available);
  assert(bounded_observation.truncated);
  assert(bounded_observation.nodes.size() <= 1);
  assert(bounded_observation.relations.size() <= 1);
  assert(runtime.state_version() == observation_version);

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
    const bool recovered_opened = recovered.open(recovery_error);
    if (!recovered_opened) std::cerr << "SURGE recovery open failed: " << recovery_error << '\n';
    assert(recovered_opened);
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
    first_token.candidate.morphology.language_tag = "ja-JP";
    first_token.candidate.morphology.part_of_speech = "名詞";
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
    numeric_token.candidate.morphology.language_tag = "ja-JP";
    numeric_token.candidate.morphology.part_of_speech = "名詞,数";
    numeric_path.tokens.push_back(numeric_token);
    const auto rendered = renderer_runtime.normalize("さんじゅうに", {numeric_path}, true);
    assert(rendered.committed);
    assert(rendered.output == "32。");
  }

  // Full-span typed entities are retained in SURGE and rendered after
  // UnifiedDecision, rather than being handled by a parallel host pipeline.
  const auto date_journal = (std::filesystem::temp_directory_path() /
      "kana-decoder-surge-date-entity-test.journal").string();
  std::filesystem::remove(date_journal, ignored);
  kana_decoder::surge::JapaneseLanguageAdapter date_adapter;
  kana_decoder::SurgeRuntimeConfig date_config;
  date_config.journal_path = date_journal;
  date_config.context_id = "date-entity-integration-test";
  date_config.language_adapter = &date_adapter;
  date_config.surface_render_options.normalize_numbers = true;
  date_config.remove_journal_on_destroy = false;
  std::size_t date_graph_nodes = 0;
  std::uint64_t date_state_version = 0;
  {
    kana_decoder::SurgeNormalizationRuntime date_runtime(date_config);
    std::string date_error;
    assert(date_runtime.open(date_error));
    kana_decoder::DecodedPath date_path;
    kana_decoder::DecodedToken date_token;
    date_token.start = 0;
    date_token.end = 25;
    date_token.candidate.id = "date-token";
    date_token.candidate.surface = "日付";
    date_token.candidate.morphology.language_tag = "ja-JP";
    date_path.tokens.push_back(date_token);
    const auto normalized_date = date_runtime.normalize(
        "にせんにじゅうろくねんじゅういちがつにじゅうににち",
        {date_path}, true);
    assert(normalized_date.committed);
    assert(normalized_date.output == "2026年11月22日");
    assert(normalized_date.metrics.entities == 2);
    assert(normalized_date.metrics.evidence >= 2);
    date_graph_nodes = date_runtime.graph_node_count();
    date_state_version = date_runtime.state_version();
  }
  date_config.remove_journal_on_destroy = true;
  {
    kana_decoder::SurgeNormalizationRuntime recovered_date_runtime(date_config);
    std::string recovered_date_error;
    assert(recovered_date_runtime.open(recovered_date_error));
    assert(recovered_date_runtime.graph_node_count() == date_graph_nodes);
    assert(recovered_date_runtime.state_version() == date_state_version);
  }

  // Typed entities inside a longer sentence must contribute evidence before
  // UnifiedDecision and persist only against the claims covered by each span.
  const auto partial_entity_journal = (std::filesystem::temp_directory_path() /
      "kana-decoder-surge-partial-entity-test.journal").string();
  std::filesystem::remove(partial_entity_journal, ignored);
  kana_decoder::surge::JapaneseLanguageAdapter partial_entity_adapter;
  kana_decoder::SurgeRuntimeConfig partial_entity_config;
  partial_entity_config.journal_path = partial_entity_journal;
  partial_entity_config.context_id = "partial-entity-decision-test";
  partial_entity_config.language_adapter = &partial_entity_adapter;
  partial_entity_config.surface_render_options.normalize_numbers = true;
  partial_entity_config.remove_journal_on_destroy = true;
  {
    kana_decoder::SurgeNormalizationRuntime partial_entity_runtime(partial_entity_config);
    std::string partial_entity_error;
    assert(partial_entity_runtime.open(partial_entity_error));
    const std::string partial_date = "にせんにじゅうろくねんじゅういちがつにじゅうににち";
    const std::string partial_reading = "きょうは" + partial_date +
        "のごごさんじにじゅっぷんです";
    const auto make_partial_path = [&](const std::string& id,
                                       const std::string& date_surface,
                                       const std::string& time_surface,
                                       double score) {
      kana_decoder::DecodedPath path;
      path.score = score;
      const auto add = [&](std::size_t start, std::size_t end,
                           const std::string& token_id, const std::string& surface) {
        kana_decoder::DecodedToken token;
        token.start = start;
        token.end = end;
        token.candidate.id = id + "-" + token_id;
        token.candidate.surface = surface;
        token.candidate.morphology.language_tag = "ja-JP";
        path.tokens.push_back(token);
      };
      add(0, 4, "prefix", "今日は");
      add(4, 29, "date", date_surface);
      add(29, 30, "particle", "の");
      add(30, 41, "time", time_surface);
      add(41, 43, "suffix", "です");
      return path;
    };
    const auto decoder_top = make_partial_path(
        "decoder-top", "二千二十六年十一月二十二日", "午後三時二十分", -1.0);
    const auto entity_supported = make_partial_path(
        "entity-supported", "2026年11月22日", "15時20分", -10.0);
    const auto entity_result = partial_entity_runtime.normalize(
        partial_reading, {decoder_top, entity_supported}, true);
    if (!entity_result.committed)
      std::cerr << "partial entity normalize failed: " << entity_result.error << '\n';
    assert(entity_result.committed);
    assert(entity_result.output == "今日は2026年11月22日の15時20分です");
    assert(entity_result.metrics.typed_entities_found == 2);
    assert(entity_result.metrics.typed_entity_evidence_added == 2);
    assert(entity_result.metrics.selected_typed_entity_evidence == 2);
    assert(entity_result.metrics.typed_entities_persisted == 2);
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
    calibrated_first_token.candidate.morphology.language_tag = "ja-JP";
    calibrated_first_token.candidate.morphology.part_of_speech = "名詞";
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

  // A calibrated minimum margin must make low-confidence SURGE decisions defer.
  const auto margin_journal = (std::filesystem::temp_directory_path() /
      "kana-decoder-surge-margin-guard-test.journal").string();
  std::filesystem::remove(margin_journal, ignored);
  kana_decoder::SurgeRuntimeConfig margin_config;
  margin_config.journal_path = margin_journal;
  margin_config.context_id = "minimum-margin-guard-test";
  margin_config.minimum_recognition_margin = 1000.0;
  margin_config.remove_journal_on_destroy = true;
  {
    kana_decoder::SurgeNormalizationRuntime margin_runtime(margin_config);
    std::string margin_error;
    assert(margin_runtime.open(margin_error));
    kana_decoder::DecodedPath margin_first;
    kana_decoder::DecodedToken margin_first_token;
    margin_first_token.start = 0;
    margin_first_token.end = 5;
    margin_first_token.candidate.id = "margin-first";
    margin_first_token.candidate.surface = "交渉";
    margin_first_token.candidate.morphology.language_tag = "ja-JP";
    margin_first.tokens.push_back(margin_first_token);
    kana_decoder::DecodedPath margin_second = margin_first;
    margin_second.tokens.front().candidate.id = "margin-second";
    margin_second.tokens.front().candidate.surface = "高尚";
    const auto guarded = margin_runtime.normalize("こうしょう", {margin_first, margin_second}, true);
    assert(!guarded.committed);
    assert(guarded.error.find("SURGE_UNIFIED_DECISION_DEFERRED:") == 0);
  }

  // Projection-only API must ingest candidate cognition without invoking
  // UnifiedDecision or producing/committing a selected decoder output.
  const projection_journal = (std::filesystem::temp_directory_path() /
      "kana-decoder-surge-projection-only-test.journal").string();
  std::filesystem::remove(projection_journal, ignored);
  kana_decoder::SurgeRuntimeConfig projection_config;
  projection_config.journal_path = projection_journal;
  projection_config.context_id = "projection-only-test";
  projection_config.remove_journal_on_destroy = true;
  {
    kana_decoder::SurgeNormalizationRuntime projection_runtime(projection_config);
    std::string projection_error;
    assert(projection_runtime.open(projection_error));
    kana_decoder::DecodedPath projection_path;
    kana_decoder::DecodedToken projection_token;
    projection_token.start = 0;
    projection_token.end = 2;
    projection_token.candidate.id = "projection-kana";
    projection_token.candidate.surface = "仮名";
    projection_path.tokens.push_back(projection_token);
    const auto before_projection = projection_runtime.state_version();
    const auto projected = projection_runtime.project_candidates("かな", {projection_path});
    assert(projected.projected);
    assert(projected.claims_added == 1);
    assert(projected.relations_added >= 1);
    assert(projected.state_version > before_projection);
    const auto observed = projection_runtime.observe();
    assert(observed.available);
    bool found_projection_claim = false;
    for (const auto& node : observed.nodes)
      if (node.kind == ::surge::NodeKind::Claim &&
          node.content.find("candidate interpretation: 仮名") == 0)
        found_projection_claim = true;
    assert(found_projection_claim);
    const auto after_projection_observation = projection_runtime.state_version();
    (void)projection_runtime.observe(1, 1);
    assert(projection_runtime.state_version() == after_projection_observation);
  }

  return 0;
}
