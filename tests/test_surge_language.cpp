#include "kana_decoder/surge_language.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

using namespace kana_decoder::surge;

int main() {
  LanguageState state;
  Hypothesis noun;
  assert(noun.id.assign("h-noun"));
  assert(noun.surface.assign("京都"));
  assert(noun.interpretation.assign("place:kyoto"));
  noun.span = {0, 3};
  noun.confidence = 0.8;
  assert(state.add_hypothesis(noun));

  Hypothesis predicate;
  assert(predicate.id.assign("h-go"));
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
  assert(state.evidence_count() == 3);
  assert(state.hypothesis_count() == 2);
  assert(state.entity_count() == 3);
  assert(state.memory_bytes() >= sizeof(LanguageState));

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
