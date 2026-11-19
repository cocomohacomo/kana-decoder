#include "kana_decoder/japanese_language.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

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
      (value.value < 0 || value.value > 23 || value.secondary < 0 ||
       value.secondary > 59 || value.tertiary < 0 || value.tertiary > 59)) return false;
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

const Hypothesis* LanguageState::find_hypothesis_for_candidate(std::uint16_t start, std::uint16_t end,
                                                                std::string_view candidate_id,
                                                                std::string_view surface) const {
  for (std::size_t i = 0; i < hypothesis_count_; ++i) {
    const auto& item = hypotheses_[i];
    if (item.span.start == start && item.span.end == end &&
        item.candidate_id.equals(candidate_id) && item.surface.equals(surface)) return &item;
  }
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

bool rerank_paths(std::vector<::kana_decoder::DecodedPath>& paths,
                  const LanguageState& state, const EvidencePolicy& policy,
                  std::uint64_t revision, double evidence_weight,
                  PathRerankMetrics* metrics) {
  if (paths.size() > 8 || !std::isfinite(evidence_weight) || evidence_weight < 0.0) return false;
  if (metrics) *metrics = {};
  if (paths.size() < 2 || evidence_weight == 0.0) return true;
  std::array<double, 8> ranks{};
  std::array<double, 8> evidence_scores{};
  std::array<bool, 8> has_evidence{};
  std::array<std::size_t, 8> order{};
  for (std::size_t i = 0; i < paths.size(); ++i) {
    order[i] = i;
    const auto& path = paths[i];
    double evidence_sum = 0.0;
    std::size_t evidence_tokens = 0;
    for (const auto& token : path.tokens) {
      if (token.start > 65535 || token.end > 65535) continue;
      const auto* hypothesis = state.find_hypothesis_for_candidate(
          static_cast<std::uint16_t>(token.start), static_cast<std::uint16_t>(token.end),
          token.candidate.id, token.candidate.surface);
      if (!hypothesis) continue;
      double token_score = 0.0;
      if (state.aggregate_evidence(hypothesis->id.str(), revision, policy, token_score)) {
        evidence_sum += token_score;
        ++evidence_tokens;
      }
    }
    if (evidence_tokens > 0) {
      has_evidence[i] = true;
      evidence_scores[i] = evidence_sum / static_cast<double>(evidence_tokens);
      ranks[i] = path.tokens.empty() ? path.score :
          path.score / static_cast<double>(path.tokens.size()) + evidence_weight * evidence_scores[i];
      if (metrics) ++metrics->supported_paths;
    } else {
      ranks[i] = path.tokens.empty() ? path.score : path.score / static_cast<double>(path.tokens.size());
    }
  }
  for (std::size_t i = 1; i < paths.size(); ++i) {
    const std::size_t current = order[i];
    std::size_t j = i;
    while (j > 0 && ranks[current] > ranks[order[j - 1]]) {
      order[j] = order[j - 1];
      --j;
    }
    order[j] = current;
  }
  if (metrics) {
    metrics->order_changed = order[0] != 0;
    metrics->best_evidence_score = has_evidence[order[0]] ? evidence_scores[order[0]] : 0.0;
  }
  std::array<::kana_decoder::DecodedPath, 8> sorted{};
  for (std::size_t i = 0; i < paths.size(); ++i) sorted[i] = paths[order[i]];
  for (std::size_t i = 0; i < paths.size(); ++i) paths[i] = std::move(sorted[i]);
  return true;
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

bool JapaneseLanguageAdapter::make_hypothesis(const ::kana_decoder::Candidate& candidate,
                                               double candidate_score, Span span,
                                               std::string_view hypothesis_id,
                                               Hypothesis& output) const {
  if (!span.valid() || candidate.id.empty() || candidate.surface.empty() ||
      (!candidate.morphology.language_tag.empty() &&
       candidate.morphology.language_tag != language_tag())) return false;
  Hypothesis value;
  if (!value.id.assign(hypothesis_id) || !value.candidate_id.assign(candidate.id) ||
      !value.surface.assign(candidate.surface)) return false;
  const std::string_view interpretation = candidate.morphology.part_of_speech.empty()
      ? std::string_view("lexical") :
        std::string_view(candidate.morphology.part_of_speech);
  if (!value.interpretation.assign(interpretation)) return false;
  value.span = span;
  if (!std::isfinite(candidate_score)) return false;
  value.score = candidate_score;
  output = value;
  return true;
}

bool JapaneseLanguageAdapter::make_entity(std::string_view reading,
    std::string_view entity_id, EntityTarget target, std::string_view unit,
    Entity& output) const {
  if (reading.empty() || reading.size() > 128 || entity_id.empty()) return false;
  auto parse_integer = [](std::string_view input, std::int64_t& result) {
    ParsedNumber parsed;
    if (!parse_japanese_number(input, parsed) || parsed.decimal_places != 0) return false;
    result = parsed.mantissa;
    return true;
  };
  Entity value;
  if (!value.id.assign(entity_id)) return false;

  if (target == EntityTarget::Identifier) {
    if (!unit.empty()) return false;
    std::string digits;
    auto remaining = reading;
    while (!remaining.empty()) {
      const auto* token = match_number_token(remaining);
      if (!token || token->kind != NumberTokenKind::Digit) return false;
      digits.push_back(static_cast<char>('0' + token->value));
      remaining.remove_prefix(token->text.size());
    }
    if (digits.empty() || !value.raw.assign(digits)) return false;
    value.kind = EntityKind::Identifier;
    value.valid = true;
    output = value;
    return true;
  }

  if (target == EntityTarget::Date) {
    if (!unit.empty()) return false;
    const auto year_end = reading.find("ねん");
    if (year_end == std::string_view::npos) return false;
    const auto month_start = year_end + std::string_view("ねん").size();
    const auto month_end = reading.find("がつ", month_start);
    if (month_end == std::string_view::npos) return false;
    const auto day_start = month_end + std::string_view("がつ").size();
    if (day_start >= reading.size()) return false;
    std::int64_t year = 0, month = 0, day = 0;
    if (!parse_integer(reading.substr(0, year_end), year) ||
        !parse_integer(reading.substr(month_start, month_end - month_start), month)) return false;
    const auto day_reading = reading.substr(day_start);
    constexpr std::pair<std::string_view, std::int64_t> special_days[] = {
      {"ついたち",1},{"ふつか",2},{"みっか",3},{"よっか",4},{"いつか",5},
      {"むいか",6},{"なのか",7},{"ようか",8},{"ここのか",9},{"とおか",10},
      {"じゅうよっか",14},{"じゅうくにち",19},{"はつか",20}
    };
    bool special = false;
    for (const auto& item : special_days) {
      if (day_reading == item.first) { day = item.second; special = true; break; }
    }
    if (!special) {
      if (day_reading.size() < std::string_view("にち").size() ||
          day_reading.substr(day_reading.size() - std::string_view("にち").size()) != "にち")
        return false;
      const auto numeric_day = day_reading.substr(0,
          day_reading.size() - std::string_view("にち").size());
      if (!parse_integer(numeric_day, day)) return false;
    }
    if (!valid_date(year, month, day) || !value.raw.assign(reading)) return false;
    value.kind = EntityKind::Date;
    value.value = year;
    value.secondary = month;
    value.tertiary = day;
    value.valid = true;
    output = value;
    return true;
  }

  if (target == EntityTarget::Time) {
    if (!unit.empty()) return false;
    bool pm = false, am = false;
    auto time_reading = reading;
    if (time_reading.substr(0, std::string_view("ごご").size()) == "ごご") {
      pm = true;
      time_reading.remove_prefix(std::string_view("ごご").size());
    } else if (time_reading.substr(0, std::string_view("ごぜん").size()) == "ごぜん") {
      am = true;
      time_reading.remove_prefix(std::string_view("ごぜん").size());
    }
    const auto hour_end = time_reading.find("じ");
    if (hour_end == std::string_view::npos) return false;
    std::int64_t hour = 0, minute = 0;
    if (!parse_integer(time_reading.substr(0, hour_end), hour)) return false;
    auto tail = time_reading.substr(hour_end + std::string_view("じ").size());
    if (!tail.empty()) {
      const bool has_fun = tail.size() >= std::string_view("ふん").size() &&
          tail.substr(tail.size() - std::string_view("ふん").size()) == "ふん";
      const bool has_pun = tail.size() >= std::string_view("ぷん").size() &&
          tail.substr(tail.size() - std::string_view("ぷん").size()) == "ぷん";
      if (!has_fun && !has_pun) return false;
      tail.remove_suffix(has_fun ? std::string_view("ふん").size() :
                                   std::string_view("ぷん").size());
      if (!parse_integer(tail, minute)) return false;
    }
    if (hour < 0 || hour > 24 || minute < 0 || minute > 59) return false;
    if (pm) {
      if (hour < 1 || hour > 12) return false;
      if (hour != 12) hour += 12;
    } else if (am) {
      if (hour < 1 || hour > 12) return false;
      if (hour == 12) hour = 0;
    } else if (hour > 23) {
      return false;
    }
    if (!value.raw.assign(reading)) return false;
    value.kind = EntityKind::Time;
    value.value = hour;
    value.secondary = minute;
    value.valid = true;
    output = value;
    return true;
  }

  if (target == EntityTarget::Range) {
    const auto separator = reading.find("から");
    if (separator == std::string_view::npos ||
        reading.find("から", separator + std::string_view("から").size()) != std::string_view::npos)
      return false;
    ParsedNumber first, second;
    if (!parse_japanese_number(reading.substr(0, separator), first) ||
        !parse_japanese_number(reading.substr(separator + std::string_view("から").size()), second))
      return false;
    const auto places = std::max(first.decimal_places, second.decimal_places);
    auto scale = [](std::int64_t& number, std::uint8_t from, std::uint8_t to) {
      for (std::uint8_t i = from; i < to; ++i) {
        if (number > std::numeric_limits<std::int64_t>::max() / 10 ||
            number < std::numeric_limits<std::int64_t>::min() / 10) return false;
        number *= 10;
      }
      return true;
    };
    if (!scale(first.mantissa, first.decimal_places, places) ||
        !scale(second.mantissa, second.decimal_places, places) ||
        first.mantissa > second.mantissa ||
        !value.raw.assign(reading) || (!unit.empty() && !value.unit.assign(unit))) return false;
    value.kind = EntityKind::Range;
    value.value = first.mantissa;
    value.secondary = second.mantissa;
    value.decimal_places = places;
    value.valid = true;
    output = value;
    return true;
  }

  ParsedNumber parsed;
  if ((target == EntityTarget::Number && !unit.empty()) ||
      (target != EntityTarget::Number && unit.empty()) ||
      !parse_japanese_number(reading, parsed)) return false;
  if (!value.raw.assign(reading)) return false;
  switch (target) {
    case EntityTarget::Number: value.kind = EntityKind::Number; break;
    case EntityTarget::Percent: value.kind = EntityKind::Percent; break;
    case EntityTarget::Currency: value.kind = EntityKind::Currency; break;
    case EntityTarget::Unit: value.kind = EntityKind::Unit; break;
    case EntityTarget::Date:
    case EntityTarget::Time:
    case EntityTarget::Range:
    case EntityTarget::Identifier: return false;
  }
  if (target != EntityTarget::Number && !value.unit.assign(unit)) return false;
  value.value = parsed.mantissa;
  value.decimal_places = parsed.decimal_places;
  value.valid = true;
  output = value;
  return true;
}

bool JapaneseLanguageAdapter::render_entity(const Entity& entity, NumericStyle style,
    std::string& output) const {
  return kana_decoder::surge::render_entity(entity, style, output);
}

bool JapaneseLanguageAdapter::render_path(
    const ::kana_decoder::DecodedPath& path, std::string_view reading,
    const SurfaceRenderOptions& options, std::string& output) const {
  if (path.tokens.empty() || reading.empty() ||
      !::kana_decoder::is_valid_utf8(std::string(reading))) return false;

  std::size_t reading_codepoints = 0;
  for (std::size_t i = 0; i < reading.size();) {
    const auto c = static_cast<unsigned char>(reading[i]);
    i += c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2 :
         (c & 0xf0) == 0xe0 ? 3 : 4;
    ++reading_codepoints;
  }
  const auto slice = [&](std::size_t start, std::size_t end, std::string& value) {
    if (start >= end || end > reading_codepoints) return false;
    std::size_t codepoint = 0;
    std::size_t byte_start = std::string_view::npos;
    std::size_t byte_end = std::string_view::npos;
    for (std::size_t i = 0; i < reading.size();) {
      if (codepoint == start) byte_start = i;
      if (codepoint == end) { byte_end = i; break; }
      const auto c = static_cast<unsigned char>(reading[i]);
      i += c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2 :
           (c & 0xf0) == 0xe0 ? 3 : 4;
      ++codepoint;
    }
    if (byte_end == std::string_view::npos && codepoint == end) byte_end = reading.size();
    if (byte_start == std::string_view::npos || byte_end == std::string_view::npos ||
        byte_end <= byte_start) return false;
    value.assign(reading.substr(byte_start, byte_end - byte_start));
    return true;
  };
  const auto ascii_word = [](char c) {
    const auto value = static_cast<unsigned char>(c);
    return value < 0x80 && std::isalnum(value) != 0;
  };
  const auto punctuation_prefix = [](const std::string& value) {
    return value.rfind("。", 0) == 0 || value.rfind("、", 0) == 0 ||
           value.rfind("！", 0) == 0 || value.rfind("？", 0) == 0 ||
           value.rfind(",", 0) == 0 || value.rfind(".", 0) == 0 ||
           value.rfind("!", 0) == 0 || value.rfind("?", 0) == 0;
  };

  std::string rendered;
  std::size_t previous_end = 0;
  for (const auto& token : path.tokens) {
    if (token.start != previous_end || token.start >= token.end ||
        token.candidate.surface.empty()) return false;
    std::string token_reading;
    if (!slice(token.start, token.end, token_reading)) return false;
    std::string surface = token.candidate.surface;
    if (options.normalize_numbers) {
      Entity numeric;
      std::string numeric_surface;
      if (make_numeric_entity(token_reading, "render-number-" +
              std::to_string(token.start), NumericEntityTarget::Number, {}, numeric) &&
          render_entity(numeric, options.numeric_style, numeric_surface)) {
        surface.swap(numeric_surface);
      }
    }
    if (options.space_latin_boundaries && !rendered.empty() && !surface.empty()) {
      const char previous = rendered.back();
      const char current = surface.front();
      const bool boundary = (ascii_word(previous) && static_cast<unsigned char>(current) >= 0x80 &&
                             !punctuation_prefix(surface)) ||
                            (static_cast<unsigned char>(previous) >= 0x80 && ascii_word(current));
      if (boundary) rendered.push_back(' ');
    }
    rendered += surface;
    previous_end = token.end;
  }
  if (previous_end != reading_codepoints) return false;
  if (options.normalize_numbers) {
    Entity structured;
    std::string structured_surface;
    if (make_entity(reading, "render-date", EntityTarget::Date, {}, structured) &&
        render_entity(structured, options.numeric_style, structured_surface)) {
      rendered.swap(structured_surface);
    } else if (make_entity(reading, "render-time", EntityTarget::Time, {}, structured) &&
               render_entity(structured, options.numeric_style, structured_surface)) {
      rendered.swap(structured_surface);
    } else if (make_entity(reading, "render-range", EntityTarget::Range, {}, structured) &&
               render_entity(structured, options.numeric_style, structured_surface)) {
      rendered.swap(structured_surface);
    }
  }
  if (options.append_terminal_punctuation) {
    const auto punctuation = terminal_punctuation(options.discourse_act, options.punctuation_style);
    if (!punctuation.empty()) {
      const bool already_terminated = rendered.size() >= 1 &&
          (rendered.back() == '.' || rendered.back() == '!' || rendered.back() == '?');
      const bool japanese_terminated = rendered.size() >= 3 &&
          (rendered.compare(rendered.size() - 3, 3, "。") == 0 ||
           rendered.compare(rendered.size() - 3, 3, "！") == 0 ||
           rendered.compare(rendered.size() - 3, 3, "？") == 0);
      if (!already_terminated && !japanese_terminated) rendered += punctuation;
    }
  }
  if (rendered.empty()) return false;
  output.swap(rendered);
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
      if (entity.value < 0 || entity.value > 23 || entity.secondary < 0 ||
          entity.secondary > 59 || entity.tertiary < 0 || entity.tertiary > 59) return false;
      output = std::to_string(entity.value) + "時";
      if (entity.secondary != 0 || entity.tertiary != 0) {
        output += entity.secondary < 10 ? "0" : "";
        output += std::to_string(entity.secondary) + "分";
      }
      if (entity.tertiary != 0) {
        output += entity.tertiary < 10 ? "0" : "";
        output += std::to_string(entity.tertiary) + "秒";
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
