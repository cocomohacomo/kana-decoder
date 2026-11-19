#include "kana_decoder/surge_language.hpp"

#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>

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

std::string grouped_digits(std::string raw, NumericStyle style) {
  if (style == NumericStyle::Arabic || raw.size() <= 3) return raw;
  const std::size_t sign = (!raw.empty() && raw[0] == '-') ? 1 : 0;
  for (std::size_t pos = raw.size(); pos > sign + 3; pos -= 3) raw.insert(pos - 3, ",");
  return raw;
}

std::string decimal_text(std::int64_t mantissa, std::uint8_t places, NumericStyle style) {
  std::string raw = std::to_string(mantissa);
  if (places == 0) return grouped_digits(raw, style);
  const bool negative = !raw.empty() && raw.front() == '-';
  std::string digits = negative ? raw.substr(1) : raw;
  if (digits.size() <= places) digits.insert(0, static_cast<std::size_t>(places) + 1 - digits.size(), '0');
  const std::size_t split = digits.size() - places;
  std::string integer = digits.substr(0, split);
  std::string fraction = digits.substr(split);
  if (fraction.empty()) return grouped_digits((negative ? "-" : "") + integer, style);
  return grouped_digits((negative ? "-" : "") + integer, style) + "." + fraction;
}

enum class NumberTokenKind { Digit, Additive, SmallUnit, LargeUnit, Decimal };
struct NumberToken { std::string_view text; std::int64_t value; NumberTokenKind kind; };
constexpr NumberToken kNumberTokens[] = {
  {"ろっぴゃく",600,NumberTokenKind::Additive},{"はっぴゃく",800,NumberTokenKind::Additive},
  {"さんびゃく",300,NumberTokenKind::Additive},{"はっせん",8000,NumberTokenKind::Additive},
  {"さんぜん",3000,NumberTokenKind::Additive},{"いっせん",1000,NumberTokenKind::Additive},
  {"じゅっ",10,NumberTokenKind::SmallUnit},{"じっ",10,NumberTokenKind::SmallUnit},
  {"ひゃく",100,NumberTokenKind::SmallUnit},{"びゃく",100,NumberTokenKind::SmallUnit},
  {"ぴゃく",100,NumberTokenKind::SmallUnit},{"せん",1000,NumberTokenKind::SmallUnit},
  {"ぜん",1000,NumberTokenKind::SmallUnit},{"じゅう",10,NumberTokenKind::SmallUnit},
  {"ちょう",1000000000000LL,NumberTokenKind::LargeUnit},
  {"おく",100000000,NumberTokenKind::LargeUnit},{"まん",10000,NumberTokenKind::LargeUnit},
  {"てん",0,NumberTokenKind::Decimal},
  {"ぜろ",0,NumberTokenKind::Digit},{"れい",0,NumberTokenKind::Digit},
  {"いち",1,NumberTokenKind::Digit},{"に",2,NumberTokenKind::Digit},
  {"さん",3,NumberTokenKind::Digit},{"よん",4,NumberTokenKind::Digit},
  {"しち",7,NumberTokenKind::Digit},{"なな",7,NumberTokenKind::Digit},
  {"はち",8,NumberTokenKind::Digit},{"ろく",6,NumberTokenKind::Digit},
  {"きゅう",9,NumberTokenKind::Digit},{"し",4,NumberTokenKind::Digit},
  {"ご",5,NumberTokenKind::Digit},{"く",9,NumberTokenKind::Digit}
};

bool safe_add(std::int64_t& target, std::int64_t value) {
  if ((value > 0 && target > std::numeric_limits<std::int64_t>::max() - value) ||
      (value < 0 && target < std::numeric_limits<std::int64_t>::min() - value)) return false;
  target += value;
  return true;
}
bool safe_multiply(std::int64_t a, std::int64_t b, std::int64_t& result) {
  if (a < 0 || b < 0) return false;
  if (a != 0 && b > std::numeric_limits<std::int64_t>::max() / a) return false;
  result = a * b;
  return true;
}
const NumberToken* match_number_token(std::string_view input) {
  for (const auto& token : kNumberTokens)
    if (input.substr(0, token.text.size()) == token.text) return &token;
  return nullptr;
}
bool parse_integer_part(std::string_view input, std::int64_t& output) {
  if (input.empty()) { output = 0; return true; }
  std::int64_t total = 0, group = 0, pending = -1;
  std::int64_t last_small = 10000, last_large = std::numeric_limits<std::int64_t>::max();
  while (!input.empty()) {
    const auto* token = match_number_token(input);
    if (!token || token->kind == NumberTokenKind::Decimal) return false;
    input.remove_prefix(token->text.size());
    if (token->kind == NumberTokenKind::Digit) {
      if (pending >= 0) return false;
      pending = token->value;
    } else if (token->kind == NumberTokenKind::Additive) {
      if (pending >= 0 && !safe_add(group, pending)) return false;
      pending = -1;
      if (!safe_add(group, token->value)) return false;
    } else if (token->kind == NumberTokenKind::SmallUnit) {
      if (token->value >= last_small) return false;
      const std::int64_t multiplier = pending < 0 ? 1 : pending;
      std::int64_t part = 0;
      if (!safe_multiply(multiplier, token->value, part) || !safe_add(group, part)) return false;
      pending = -1;
      last_small = token->value;
    } else {
      if (token->value >= last_large) return false;
      if (pending >= 0 && !safe_add(group, pending)) return false;
      if (group == 0) group = 1;
      std::int64_t part = 0;
      if (!safe_multiply(group, token->value, part) || !safe_add(total, part)) return false;
      group = 0; pending = -1;
      last_small = 10000;
      last_large = token->value;
    }
  }
  if (pending >= 0 && !safe_add(group, pending)) return false;
  if (!safe_add(total, group)) return false;
  output = total;
  return true;
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
  if (entity_count_ >= entities_.size() || value.id.empty() || !value.valid || value.decimal_places > 9 ||
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

bool LanguageState::add_evidence(const Evidence& value) {
  if (evidence_count_ >= evidence_.size() || value.id.empty() || value.source.empty() ||
      value.dependency_count > kMaxDependenciesPerEvidence ||
      !find_hypothesis(value.hypothesis_id.str()) ||
      !valid_score(value.score) || !valid_confidence(value.confidence) ||
      (value.valid_until_revision != 0 && value.valid_until_revision < value.valid_from_revision) ||
      has_id(evidence_, evidence_count_, value.id.str())) return false;
  for (std::size_t i = 0; i < value.dependency_count; ++i) {
    if (!find_evidence(value.dependency_ids[i].str())) return false;
    for (std::size_t j = 0; j < i; ++j)
      if (value.dependency_ids[i].equals(value.dependency_ids[j].str())) return false;
  }
  evidence_[evidence_count_++] = value;
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
  for (std::size_t i = 0; i < value.evidence_count; ++i)
    if (!find_evidence(value.evidence_ids[i].str())) return false;
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

const Evidence* LanguageState::find_evidence(std::string_view id) const {
  for (std::size_t i = 0; i < evidence_count_; ++i)
    if (evidence_[i].id.equals(id)) return &evidence_[i];
  return nullptr;
}

bool LanguageState::aggregate_evidence(std::string_view hypothesis_id, std::uint64_t revision,
                                       const EvidencePolicy& policy, double& score) const {
  if (!find_hypothesis(hypothesis_id)) return false;
  std::array<double, 10> best{};
  std::array<bool, 10> present{};
  double weighted_sum = 0.0;
  double weight_sum = 0.0;
  for (std::size_t i = 0; i < policy.kind_weights.size(); ++i) {
    if (!std::isfinite(policy.kind_weights[i]) || policy.kind_weights[i] < 0.0) return false;
  }
  for (std::size_t i = 0; i < evidence_count_; ++i) {
    const auto& item = evidence_[i];
    if (!item.hypothesis_id.equals(hypothesis_id) || revision < item.valid_from_revision ||
        (item.valid_until_revision != 0 && revision > item.valid_until_revision)) continue;
    const auto kind = static_cast<std::size_t>(item.kind);
    if (kind >= best.size()) return false;
    const double contribution = item.score * item.confidence;
    if (!present[kind] || contribution > best[kind]) {
      best[kind] = contribution;
      present[kind] = true;
    }
  }
  for (std::size_t i = 0; i < best.size(); ++i) {
    if (!present[i] || policy.kind_weights[i] == 0.0) continue;
    weighted_sum += best[i] * policy.kind_weights[i];
    weight_sum += policy.kind_weights[i];
  }
  if (weight_sum == 0.0) return false;
  score = weighted_sum / weight_sum;
  return std::isfinite(score);
}

std::size_t LanguageState::memory_bytes() const {
  return sizeof(*this);
}

bool parse_japanese_number(std::string_view reading, ParsedNumber& result) {
  if (reading.empty() || reading.size() > 128) return false;
  const auto decimal_pos = reading.find("てん");
  if (decimal_pos != std::string_view::npos &&
      reading.find("てん", decimal_pos + std::string_view("てん").size()) != std::string_view::npos) return false;
  const auto integer_part = decimal_pos == std::string_view::npos ? reading : reading.substr(0, decimal_pos);
  const auto fraction_part = decimal_pos == std::string_view::npos
                                 ? std::string_view{} : reading.substr(decimal_pos + std::string_view("てん").size());
  if (decimal_pos != std::string_view::npos && fraction_part.empty()) return false;
  std::int64_t integer = 0;
  if (!parse_integer_part(integer_part, integer)) return false;
  std::int64_t fraction = 0;
  std::uint8_t places = 0;
  auto remaining = fraction_part;
  while (!remaining.empty()) {
    const auto* token = match_number_token(remaining);
    if (!token || token->kind != NumberTokenKind::Digit || places >= 9) return false;
    if (fraction > (std::numeric_limits<std::int64_t>::max() - token->value) / 10) return false;
    fraction = fraction * 10 + token->value;
    ++places;
    remaining.remove_prefix(token->text.size());
  }
  std::int64_t scale = 1;
  for (std::uint8_t i = 0; i < places; ++i) {
    if (scale > std::numeric_limits<std::int64_t>::max() / 10) return false;
    scale *= 10;
  }
  std::int64_t mantissa = 0;
  if (!safe_multiply(integer, scale, mantissa) || !safe_add(mantissa, fraction)) return false;
  result.mantissa = mantissa;
  result.decimal_places = places;
  return true;
}

bool render_entity(const Entity& entity, NumericStyle style, std::string& output) {
  if (!entity.valid) return false;
  output.clear();
  switch (entity.kind) {
    case EntityKind::Number:
      output = decimal_text(entity.value, entity.decimal_places, style);
      break;
    case EntityKind::Percent:
      output = decimal_text(entity.value, entity.decimal_places, style) + entity.unit.str();
      break;
    case EntityKind::Currency:
      output = entity.unit.equals("円") || entity.unit.equals("％") || entity.unit.equals("%")
                   ? decimal_text(entity.value, entity.decimal_places, style) + entity.unit.str()
                   : entity.unit.str() + decimal_text(entity.value, entity.decimal_places, style);
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
      output = decimal_text(entity.value, entity.decimal_places, style) + "〜" +
               decimal_text(entity.secondary, entity.decimal_places, style) + entity.unit.str();
      break;
    case EntityKind::Unit:
      if (entity.unit.empty()) return false;
      output = decimal_text(entity.value, entity.decimal_places, style) + entity.unit.str();
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
