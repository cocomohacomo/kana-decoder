#include "kana_decoder/japanese_language.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

using namespace kana_decoder::surge;

int main() {
  LanguageState state;
  std::string rendered;
  Hypothesis noun;
  assert(noun.id.assign("h-noun"));
  assert(noun.candidate_id.assign("cand-kyoto"));
  assert(noun.surface.assign("京都"));
  assert(noun.interpretation.assign("place:kyoto"));
  noun.span = {0, 3};
  noun.confidence = 0.8;
  assert(state.add_hypothesis(noun));

  Hypothesis predicate;
  assert(predicate.id.assign("h-go"));
  assert(predicate.candidate_id.assign("cand-go"));
  assert(predicate.surface.assign("行く"));
  assert(predicate.interpretation.assign("motion:go"));
  predicate.span = {3, 5};
  predicate.confidence = 0.9;
  assert(state.add_hypothesis(predicate));

  CandidateSet candidates;
  assert(candidates.id.assign("set-1"));
  candidates.span = {0, 3};
  assert(candidates.hypothesis_ids[0].assign("h-noun"));
  candidates.count = 1;
  assert(state.add_candidate_set(candidates));

  SemanticFrame frame;
  assert(frame.id.assign("frame-1"));
  assert(frame.predicate_hypothesis.assign("h-go"));
  assert(frame.roles[0].role.assign("destination"));
  assert(frame.roles[0].target_hypothesis.assign("h-noun"));
  frame.role_count = 1;
  assert(state.add_semantic_frame(frame));

  SyntaxRelation relation;
  assert(relation.id.assign("rel-1"));
  assert(relation.head_hypothesis.assign("h-go"));
  assert(relation.dependent_hypothesis.assign("h-noun"));
  assert(relation.relation.assign("目的地"));
  assert(state.add_syntax_relation(relation));

  OrthographicHypothesis orth;
  assert(orth.id.assign("orth-1"));
  assert(orth.source_hypothesis.assign("h-noun"));
  assert(orth.surface.assign("きょうと"));
  orth.script = Script::Hiragana;
  assert(state.add_orthographic_hypothesis(orth));

  JapaneseLanguageAdapter ja_adapter;
  assert(ja_adapter.language_tag() == "ja-JP");
  ::kana_decoder::Candidate adapter_candidate;
  adapter_candidate.id = "candidate-adapter";
  adapter_candidate.surface = "京都";
  adapter_candidate.morphology.language_tag = "ja-JP";
  adapter_candidate.morphology.part_of_speech = "名詞/固有名詞";
  Hypothesis adapted_hypothesis;
  assert(ja_adapter.make_hypothesis(adapter_candidate, 8.0, {0, 3}, "adapted-1", adapted_hypothesis));
  assert(adapted_hypothesis.candidate_id.equals("candidate-adapter"));
  assert(adapted_hypothesis.interpretation.equals("名詞/固有名詞"));
  assert(adapted_hypothesis.score == 8.0);
  auto foreign_candidate = adapter_candidate;
  foreign_candidate.morphology.language_tag = "en-US";
  Hypothesis foreign_hypothesis;
  assert(!ja_adapter.make_hypothesis(foreign_candidate, 8.0, {0, 3},
                                     "foreign-1", foreign_hypothesis));

  ParsedNumber parsed;
  assert(parse_japanese_number("さんじゅうに", parsed));
  assert(parsed.mantissa == 32 && parsed.decimal_places == 0);
  assert(parse_japanese_number("ごせん", parsed));
  assert(parsed.mantissa == 5000 && parsed.decimal_places == 0);
  assert(parse_japanese_number("いちまんにせんさんびゃく", parsed));
  assert(parsed.mantissa == 12300 && parsed.decimal_places == 0);
  assert(parse_japanese_number("にてんご", parsed));
  assert(parsed.mantissa == 25 && parsed.decimal_places == 1);
  assert(!parse_japanese_number("にてん", parsed));
  assert(!parse_japanese_number("さんじゅうじゅう", parsed));

  Entity decimal;
  assert(decimal.id.assign("decimal-1"));
  decimal.kind = EntityKind::Number;
  decimal.value = 250;
  decimal.decimal_places = 2;
  decimal.valid = true;
  assert(state.add_entity(decimal));
  assert(render_entity(decimal, NumericStyle::GroupedArabic, rendered));
  assert(rendered == "2.50");

  Entity adapted_number;
  assert(ja_adapter.make_numeric_entity("さんじゅうに", "age-1",
      NumericEntityTarget::Unit, "歳", adapted_number));
  assert(adapted_number.value == 32 && adapted_number.unit.equals("歳"));

  Entity parsed_date;
  assert(ja_adapter.make_entity("にせんにじゅうろくねんじゅういちがつにじゅうににち",
      "parsed-date-1", EntityTarget::Date, {}, parsed_date));
  assert(parsed_date.kind == EntityKind::Date && parsed_date.value == 2026 &&
         parsed_date.secondary == 11 && parsed_date.tertiary == 22);
  assert(ja_adapter.render_entity(parsed_date, NumericStyle::Arabic, rendered));
  assert(rendered == "2026年11月22日");
  Entity invalid_parsed_date;
  assert(!ja_adapter.make_entity("にせんにじゅうごねんにがつさんじゅうににち",
      "invalid-date-1", EntityTarget::Date, {}, invalid_parsed_date));

  Entity parsed_time;
  assert(ja_adapter.make_entity("ごごさんじにじゅっぷん",
      "parsed-time-1", EntityTarget::Time, {}, parsed_time));
  assert(parsed_time.value == 15 && parsed_time.secondary == 20);
  assert(ja_adapter.render_entity(parsed_time, NumericStyle::Arabic, rendered));
  assert(rendered == "15時20分");
  Entity invalid_parsed_time;
  assert(!ja_adapter.make_entity("にじゅうごじ",
      "invalid-time-1", EntityTarget::Time, {}, invalid_parsed_time));

  Entity parsed_range;
  assert(ja_adapter.make_entity("さんからご", "range-1",
      EntityTarget::Range, "歳", parsed_range));
  assert(parsed_range.value == 3 && parsed_range.secondary == 5);
  assert(ja_adapter.render_entity(parsed_range, NumericStyle::Arabic, rendered));
  assert(rendered == "3〜5歳");
  Entity invalid_range;
  assert(!ja_adapter.make_entity("ごからさん", "invalid-range-1",
      EntityTarget::Range, {}, invalid_range));

  Entity parsed_identifier;
  assert(ja_adapter.make_entity("ぜろいちにさん", "identifier-1",
      EntityTarget::Identifier, {}, parsed_identifier));
  assert(ja_adapter.render_entity(parsed_identifier, NumericStyle::Arabic, rendered));
  assert(rendered == "0123");

  Entity date;
  assert(date.id.assign("date-1"));
  date.kind = EntityKind::Date;
  date.value = 2026; date.secondary = 11; date.tertiary = 19;
  date.valid = true;
  assert(state.add_entity(date));
  assert(render_entity(date, NumericStyle::Arabic, rendered));
  assert(rendered == "2026年11月19日");

  Entity currency;
  assert(currency.id.assign("money-1"));
  currency.kind = EntityKind::Currency;
  currency.value = 125000;
  currency.valid = true;
  assert(currency.unit.assign("円"));
  assert(state.add_entity(currency));
  assert(render_entity(currency, NumericStyle::GroupedArabic, rendered));
  assert(rendered == "125,000円");

  Entity time;
  assert(time.id.assign("time-1"));
  time.kind = EntityKind::Time; time.value = 9; time.secondary = 5; time.valid = true;
  assert(state.add_entity(time));
  assert(render_entity(time, NumericStyle::Arabic, rendered));
  assert(rendered == "9時05分");
  Entity time_seconds;
  assert(time_seconds.id.assign("time-seconds-1"));
  time_seconds.kind = EntityKind::Time;
  time_seconds.value = 9; time_seconds.secondary = 5; time_seconds.tertiary = 8;
  time_seconds.valid = true;
  assert(ja_adapter.render_entity(time_seconds, NumericStyle::Arabic, rendered));
  assert(rendered == "9時05分08秒");

  Evidence lexical;
  assert(lexical.id.assign("ev-lexical"));
  assert(lexical.hypothesis_id.assign("h-noun"));
  assert(lexical.source.assign("dictionary"));
  lexical.kind = EvidenceKind::Lexical;
  lexical.score = 0.8; lexical.confidence = 1.0;
  lexical.valid_from_revision = 0; lexical.valid_until_revision = 0;
  assert(state.add_evidence(lexical));

  Evidence lexical_duplicate = lexical;
  assert(lexical_duplicate.id.assign("ev-lexical-duplicate"));
  lexical_duplicate.score = 0.6;
  assert(state.add_evidence(lexical_duplicate));

  Hypothesis alternate = noun;
  assert(alternate.id.assign("h-noun-alt"));
  assert(alternate.candidate_id.assign("cand-kyoto-kana"));
  assert(alternate.surface.assign("きょうと"));
  alternate.confidence = 0.7;
  assert(state.add_hypothesis(alternate));

  Evidence alternate_semantic;
  assert(alternate_semantic.id.assign("ev-alternate-semantic"));
  assert(alternate_semantic.hypothesis_id.assign("h-noun-alt"));
  assert(alternate_semantic.source.assign("discourse-context"));
  alternate_semantic.kind = EvidenceKind::Semantic;
  alternate_semantic.score = 1.0;
  alternate_semantic.confidence = 1.0;
  assert(state.add_evidence(alternate_semantic));

  Evidence semantic;
  assert(semantic.id.assign("ev-semantic"));
  assert(semantic.hypothesis_id.assign("h-noun"));
  assert(semantic.source.assign("predicate-role"));
  semantic.kind = EvidenceKind::Semantic;
  semantic.score = 0.5; semantic.confidence = 0.8;
  semantic.valid_from_revision = 1; semantic.valid_until_revision = 3;
  assert(semantic.dependency_ids[0].assign("ev-lexical"));
  semantic.dependency_count = 1;
  assert(state.add_evidence(semantic));

  EvidencePolicy policy;
  double evidence_score = 0.0;
  assert(state.aggregate_evidence("h-noun", 2, policy, evidence_score));
  assert(std::abs(evidence_score - 0.6) < 1e-9);
  assert(state.aggregate_evidence("h-noun", 4, policy, evidence_score));
  assert(std::abs(evidence_score - 0.8) < 1e-9);

  Decision decision;
  assert(decision.id.assign("decision-1"));
  assert(decision.selected_hypothesis.assign("h-noun"));
  assert(decision.compared_hypotheses[0].assign("h-noun"));
  decision.compared_count = 1;
  assert(decision.evidence_ids[0].assign("ev-lexical"));
  decision.evidence_count = 1;
  decision.confidence = 0.8;
  assert(state.add_decision(decision));

  Revision revision;
  assert(revision.id.assign("revision-1"));
  assert(revision.target_id.assign("output-span-1"));
  assert(revision.replacement_hypothesis.assign("h-noun"));
  assert(revision.reason.assign("later-context"));
  assert(state.add_revision(revision));

  CommitSpan commit;
  commit.span = {0, 3};
  assert(commit.output.assign("京都"));
  assert(state.add_commit_span(commit));
  CommitSpan overlap;
  overlap.span = {2, 4};
  assert(overlap.output.assign("京都へ"));
  assert(!state.add_commit_span(overlap));

  assert(state.find_hypothesis("h-go") != nullptr);
  assert(state.find_entity("date-1") != nullptr);
  assert(state.find_evidence("ev-semantic") != nullptr);
  assert(state.evidence_count() == 4);
  assert(state.hypothesis_count() == 3);
  assert(state.entity_count() == 4);
  assert(state.memory_bytes() >= sizeof(LanguageState));
  assert(state.memory_bytes() <= 256 * 1024);

  std::vector<::kana_decoder::DecodedPath> too_many_paths(9);
  assert(!rerank_paths(too_many_paths, state, policy, 2, 1.0, nullptr));

  ::kana_decoder::DecodedPath first_path;
  first_path.score = -200.0;
  first_path.tokens.push_back({0, 3, {"cand-kyoto", "京都"}, 0.0, 0.0});
  ::kana_decoder::DecodedPath second_path;
  second_path.score = -210.0;
  second_path.tokens.push_back({0, 3, {"cand-kyoto-kana", "きょうと"}, 0.0, 0.0});
  std::vector<::kana_decoder::DecodedPath> paths{first_path, second_path};
  PathRerankMetrics rerank_metrics;
  assert(rerank_paths(paths, state, policy, 2, 30.0, &rerank_metrics));
  assert(rerank_metrics.supported_paths == 2);
  assert(rerank_metrics.order_changed);
  assert(paths.front().tokens.front().candidate.id == "cand-kyoto-kana");
  assert(!rerank_paths(paths, state, policy, 2, -1.0, nullptr));

  Entity invalid_date;
  assert(invalid_date.id.assign("bad-date"));
  invalid_date.kind = EntityKind::Date;
  invalid_date.value = 2025; invalid_date.secondary = 2; invalid_date.tertiary = 29;
  invalid_date.valid = true;
  assert(!state.add_entity(invalid_date));

  Hypothesis invalid;
  assert(invalid.id.assign("bad-h"));
  assert(invalid.surface.assign("bad"));
  invalid.span = {4, 4};
  assert(!state.add_hypothesis(invalid));

  assert(terminal_punctuation(DiscourseAct::Question, PunctuationStyle::Japanese) == "？");
  assert(terminal_punctuation(DiscourseAct::Statement, PunctuationStyle::Japanese) == "。");
  assert(terminal_punctuation(DiscourseAct::Question, PunctuationStyle::ASCII) == "?");
  assert(terminal_punctuation(DiscourseAct::Question, PunctuationStyle::Minimal).empty());

  ::kana_decoder::DecodedPath numeric_path;
  ::kana_decoder::DecodedToken numeric_token;
  numeric_token.start = 0;
  numeric_token.end = 6;
  numeric_token.candidate.id = "numeric-32";
  numeric_token.candidate.surface = "三十二";
  numeric_path.tokens.push_back(numeric_token);
  SurfaceRenderOptions render_options;
  render_options.normalize_numbers = true;
  render_options.append_terminal_punctuation = true;
  std::string normalized_sentence;
  assert(ja_adapter.render_path(numeric_path, "さんじゅうに",
                                render_options, normalized_sentence));
  assert(normalized_sentence == "32。");
  render_options.discourse_act = DiscourseAct::Question;
  assert(ja_adapter.render_path(numeric_path, "さんじゅうに",
                                render_options, normalized_sentence));
  assert(normalized_sentence == "32？");
  render_options.normalize_numbers = false;
  render_options.append_terminal_punctuation = false;
  assert(ja_adapter.render_path(numeric_path, "さんじゅうに",
                                render_options, normalized_sentence));
  assert(normalized_sentence == "三十二");
  const auto prior_render = normalized_sentence;
  numeric_token.start = 0;
  numeric_token.end = 7;
  numeric_path.tokens.front() = numeric_token;
  assert(!ja_adapter.render_path(numeric_path, "さんじゅうに",
                                 render_options, normalized_sentence));
  assert(normalized_sentence == prior_render);

  LanguageState bounded;
  for (std::size_t i = 0; i < kMaxHypotheses; ++i) {
    Hypothesis item;
    const std::string id = "h" + std::to_string(i);
    assert(item.id.assign(id));
    assert(item.surface.assign("語"));
    item.span = {0, 1};
    assert(bounded.add_hypothesis(item));
  }
  Hypothesis overflow;
  assert(overflow.id.assign("overflow"));
  assert(overflow.surface.assign("語"));
  overflow.span = {0, 1};
  assert(!bounded.add_hypothesis(overflow));

  std::cout << "SURGE language foundation tests passed\n";
}
