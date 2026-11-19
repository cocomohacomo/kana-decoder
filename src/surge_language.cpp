#include "kana_decoder/surge_language.hpp"

#include <cmath>
#include <cstring>
#include <limits>
#include <string>

namespace kana_decoder::surge {
namespace {

bool valid_confidence(double value) {
  return std::isfinite(value) && value >= 0.0 && value <= 1.0;
}
bool valid_score(double value) { return std::isfinite(value); }

bool valid_date(std::int64_t year, std::int64_t month, std::int64_t day) {
  if (year < 1 || year > 9999 || month < 1 || month > 12 || day < 1) return false;
  static constexpr int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  int max_day = days[month - 1];
  const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
  if (month == 2 && leap) max_day = 29;
  return day <= max_day;
}

std::string integer_text(std::int64_t value, NumericStyle style) {
  std::string raw = std::to_string(value);
  if (style == NumericStyle::Arabic || raw.size() <= 3) return raw;
  const std::size_t sign = (!raw.empty() && raw[0] == '-') ? 1 : 0;
  for (std::size_t pos = raw.size(); pos > sign + 3; pos -= 3) raw.insert(pos - 3, ",");
  return raw;
}
}  // namespace

template <typename T, std::size_t N>
bool LanguageState::has_id(const std::array<T, N>& values, std::size_t count, std::string_view id) {
  for (std::size_t i = 0; i < count; ++i) if (values[i].id.equals(id)) return true;
  return false;
}

bool LanguageState::add_hypothesis(const Hypothesis& value) {
  if (hypothesis_count_ >= hypotheses_.size() || !value.span.valid() ||
      value.id.empty() || value.surface.empty() || !valid_score(value.score) ||
      !valid_confidence(value.confidence) || has_id(hypotheses_, hypothesis_count_, value.id.str())) return false;
  hypotheses_[hypothesis_count_++] = value;
  return true;
}

bool LanguageState::add_candidate_set(const CandidateSet& value) {
  if (candidate_set_count_ >= candidate_sets_.size() || value.id.empty() ||
      !value.span.valid() || value.count == 0 || value.count > kMaxCandidatesPerSet ||
      has_id(candidate_sets_, candidate_set_count_, value.id.str())) return false;
  for (std::size_t i = 0; i < value.count; ++i) {
    if (!find_hypothesis(value.hypothesis_ids[i].str()) ||
        !find_hypothesis(value.hypothesis_ids[i].str())->span.contains(value.span)) return false;
    for (std::size_t j = 0; j < i; ++j)
      if (value.hypothesis_ids[i].equals(value.hypothesis_ids[j].str())) return false;
  }
  candidate_sets_[candidate_set_count_++] = value;
  return true;
}

bool LanguageState::add_semantic_frame(const SemanticFrame& value) {
  if (frame_count_ >= frames_.size() || value.id.empty() || value.role_count > kMaxRolesPerFrame ||
      !find_hypothesis(value.predicate_hypothesis.str()) ||
      has_id(frames_, frame_count_, value.id.str())) return false;
  for (std::size_t i = 0; i < value.role_count; ++i)
    if (value.roles[i].role.empty() || !find_hypothesis(value.roles[i].target_hypothesis.str())) return false;
  frames_[frame_count_++] = value;
  return true;
}

bool LanguageState::add_syntax_relation(const SyntaxRelation& value) {
  if (relation_count_ >= relations_.size() || value.id.empty() || value.relation.empty() ||
      !find_hypothesis(value.head_hypothesis.str()) ||
      !find_hypothesis(value.dependent_hypothesis.str()) ||
      has_id(relations_, relation_count_, value.id.str())) return false;
  relations_[relation_count_++] = value;
  return true;
}

bool LanguageState::add_orthographic_hypothesis(const OrthographicHypothesis& value) {
  if (orthography_count_ >= orthography_.size() || value.id.empty() ||
      value.surface.empty() || !valid_score(value.score) ||
      !find_hypothesis(value.source_hypothesis.str()) ||
      has_id(orthography_, orthography_count_, value.id.str())) return false;
  orthography_[orthography_count_++] = value;
  return true;
}

bool LanguageState::add_entity(const Entity& value) {
  if (entity_count_ >= entities_.size() || value.id.empty() || !value.valid ||
      has_id(entities_, entity_count_, value.id.str())) return false;
  if (value.kind == EntityKind::Date && !valid_date(value.value, value.secondary, value.tertiary)) return false;
  if (value.kind == EntityKind::Time &&
      (value.value < 0 || value.value > 23 || value.secondary < 0 || value.secondary > 59)) return false;
  if ((value.kind == EntityKind::Percent || value.kind == EntityKind::Currency ||
       value.kind == EntityKind::Unit) && value.unit.empty()) return false;
  if (value.kind == EntityKind::Range && value.value > value.secondary) return false;
  entities_[entity_count_++] = value;
  return true;
}

bool LanguageState::add_constraint(const Constraint& value) {
  if (constraint_count_ >= constraints_.size() || value.id.empty() || value.rule.empty() ||
      !valid_score(value.weight) || has_id(constraints_, constraint_count_, value.id.str())) return false;
  if (!value.subject_hypothesis.empty() && !find_hypothesis(value.subject_hypothesis.str())) return false;
  if (!value.object_hypothesis.empty() && !find_hypothesis(value.object_hypothesis.str())) return false;
  constraints_[constraint_count_++] = value;
  return true;
}

bool LanguageState::add_decision(const Decision& value) {
  if (decision_count_ >= decisions_.size() || value.id.empty() ||
      value.compared_count > kMaxCandidatesPerSet || value.evidence_count > kMaxEvidencePerDecision ||
      !valid_confidence(value.confidence) ||
      !find_hypothesis(value.selected_hypothesis.str()) ||
      has_id(decisions_, decision_count_, value.id.str())) return false;
  bool selected_compared = false;
  for (std::size_t i = 0; i < value.compared_count; ++i) {
    if (!find_hypothesis(value.compared_hypotheses[i].str())) return false;
    if (value.compared_hypotheses[i].equals(value.selected_hypothesis.str())) selected_compared = true;
  }
  if (value.compared_count > 0 && !selected_compared) return false;
  for (std::size_t i = 0; i < value.evidence_count; ++i) if (value.evidence_ids[i].empty()) return false;
  decisions_[decision_count_++] = value;
  return true;
}

bool LanguageState::add_revision(const Revision& value) {
  if (revision_count_ >= revisions_.size() || value.id.empty() || value.target_id.empty() ||
      value.replacement_hypothesis.empty() || value.reason.empty() ||
      !find_hypothesis(value.replacement_hypothesis.str()) ||
      has_id(revisions_, revision_count_, value.id.str())) return false;
  if (!value.previous_hypothesis.empty() && !find_hypothesis(value.previous_hypothesis.str())) return false;
  revisions_[revision_count_++] = value;
  return true;
}

bool LanguageState::add_commit_span(const CommitSpan& value) {
  if (commit_span_count_ >= commits_.size() || !value.span.valid() || value.output.empty()) return false;
  for (std::size_t i = 0; i < commit_span_count_; ++i) {
    const auto& prior = commits_[i];
    if (value.span.start < prior.span.end && prior.span.start < value.span.end) return false;
  }
  commits_[commit_span_count_++] = value;
  return true;
}

const Hypothesis* LanguageState::find_hypothesis(std::string_view id) const {
  for (std::size_t i = 0; i < hypothesis_count_; ++i)
    if (hypotheses_[i].id.equals(id)) return &hypotheses_[i];
  return nullptr;
}

const Entity* LanguageState::find_entity(std::string_view id) const {
  for (std::size_t i = 0; i < entity_count_; ++i)
    if (entities_[i].id.equals(id)) return &entities_[i];
  return nullptr;
}

std::size_t LanguageState::memory_bytes() const {
  return sizeof(*this);
}

bool render_entity(const Entity& entity, NumericStyle style, std::string& output) {
  if (!entity.valid) return false;
  output.clear();
  switch (entity.kind) {
    case EntityKind::Number:
      output = integer_text(entity.value, style);
      break;
    case EntityKind::Percent:
      output = integer_text(entity.value, style) + entity.unit.str();
      break;
    case EntityKind::Currency:
      output = entity.unit.equals("円") || entity.unit.equals("％") || entity.unit.equals("%")
                   ? integer_text(entity.value, style) + entity.unit.str()
                   : entity.unit.str() + integer_text(entity.value, style);
      break;
    case EntityKind::Date:
      if (!valid_date(entity.value, entity.secondary, entity.tertiary)) return false;
      output = std::to_string(entity.value) + "年" + std::to_string(entity.secondary) +
               "月" + std::to_string(entity.tertiary) + "日";
      break;
    case EntityKind::Time:
      if (entity.value < 0 || entity.value > 23 || entity.secondary < 0 || entity.secondary > 59) return false;
      output = std::to_string(entity.value) + "時";
      if (entity.secondary != 0) {
        output += entity.secondary < 10 ? "0" : "";
        output += std::to_string(entity.secondary) + "分";
      }
      break;
    case EntityKind::Range:
      if (entity.value > entity.secondary) return false;
      output = integer_text(entity.value, style) + "〜" +
               integer_text(entity.secondary, style) + entity.unit.str();
      break;
    case EntityKind::Unit:
      if (entity.unit.empty()) return false;
      output = integer_text(entity.value, style) + entity.unit.str();
      break;
    case EntityKind::Identifier:
      output = entity.raw.str();
      if (output.empty()) return false;
      break;
  }
  return !output.empty();
}

std::string terminal_punctuation(DiscourseAct act, PunctuationStyle style) {
  if (style == PunctuationStyle::Minimal) return {};
  if (style == PunctuationStyle::ASCII) {
    if (act == DiscourseAct::Question) return "?";
    if (act == DiscourseAct::Exclamation) return "!";
    if (act == DiscourseAct::Statement) return ".";
    return {};
  }
  if (act == DiscourseAct::Question) return "？";
  if (act == DiscourseAct::Exclamation) return "！";
  if (act == DiscourseAct::Statement) return "。";
  return {};
}

}  // namespace kana_decoder::surge
