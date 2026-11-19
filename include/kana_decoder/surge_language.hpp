#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include "kana_decoder/engine.hpp"

namespace kana_decoder::surge {

constexpr std::size_t kMaxHypotheses = 32;
constexpr std::size_t kMaxCandidateSets = 16;
constexpr std::size_t kMaxFrames = 16;
constexpr std::size_t kMaxRelations = 64;
constexpr std::size_t kMaxEntities = 16;
constexpr std::size_t kMaxEvidence = 64;
constexpr std::size_t kMaxDependenciesPerEvidence = 4;
constexpr std::size_t kMaxConstraints = 32;
constexpr std::size_t kMaxDecisions = 32;
constexpr std::size_t kMaxRevisions = 32;
constexpr std::size_t kMaxCommitSpans = 16;
constexpr std::size_t kMaxCandidatesPerSet = 8;
constexpr std::size_t kMaxEvidencePerDecision = 8;
constexpr std::size_t kMaxRolesPerFrame = 8;

template <std::size_t N>
struct FixedText {
  std::array<char, N> bytes{};
  std::uint16_t size = 0;

  bool assign(std::string_view value) {
    if (value.size() >= N || value.find('\0') != std::string_view::npos) return false;
    bytes.fill('\0');
    for (std::size_t i = 0; i < value.size(); ++i) bytes[i] = value[i];
    size = static_cast<std::uint16_t>(value.size());
    return true;
  }
  std::string str() const { return std::string(bytes.data(), size); }
  bool empty() const { return size == 0; }
  bool equals(std::string_view value) const {
    return value.size() == size && std::string_view(bytes.data(), size) == value;
  }
};

using Id = FixedText<64>;
using Text = FixedText<192>;
using Label = FixedText<48>;

struct Span {
  std::uint16_t start = 0;
  std::uint16_t end = 0;
  bool valid() const { return start < end; }
  bool contains(const Span& other) const {
    return valid() && other.valid() && start <= other.start && end >= other.end;
  }
};

enum class HypothesisStatus : std::uint8_t { Active, Deferred, Selected, Rejected, Expired };
enum class Script : std::uint8_t { Kanji, Hiragana, Katakana, Latin, ArabicNumeral, Symbol, Mixed };
enum class EvidenceKind : std::uint8_t {
  Lexical, Ngram, PartOfSpeech, Morphological, Syntactic, Semantic,
  Discourse, Entity, Orthographic, UserPolicy
};
enum class EntityKind : std::uint8_t { Number, Percent, Currency, Date, Time, Range, Unit, Identifier };
enum class ConstraintKind : std::uint8_t { Morphology, Syntax, Semantic, Orthographic, Domain, UserPolicy };
enum class DiscourseAct : std::uint8_t { Statement, Question, Exclamation, ListItem, Continuation };
enum class PunctuationStyle : std::uint8_t { Japanese, ASCII, Minimal };

struct Hypothesis {
  Id id;
  Id candidate_id;
  Span span;
  Text surface;
  Label interpretation;
  double score = 0.0;
  double confidence = 0.0;
  std::uint64_t created_revision = 0;
  std::uint64_t valid_until_revision = 0;
  HypothesisStatus status = HypothesisStatus::Active;
};

struct CandidateSet {
  Id id;
  Span span;
  std::array<Id, kMaxCandidatesPerSet> hypothesis_ids{};
  std::uint8_t count = 0;
};

struct SemanticRole {
  Label role;
  Id target_hypothesis;
};

struct SemanticFrame {
  Id id;
  Id predicate_hypothesis;
  std::array<SemanticRole, kMaxRolesPerFrame> roles{};
  std::uint8_t role_count = 0;
};

struct SyntaxRelation {
  Id id;
  Id head_hypothesis;
  Id dependent_hypothesis;
  Label relation;
};

struct OrthographicHypothesis {
  Id id;
  Id source_hypothesis;
  Text surface;
  Script script = Script::Mixed;
  double score = 0.0;
};

struct Entity {
  Id id;
  EntityKind kind = EntityKind::Number;
  Text raw;
  Text unit;
  std::int64_t value = 0;
  std::int64_t secondary = 0;
  std::int64_t tertiary = 0;
  std::uint8_t decimal_places = 0;
  bool valid = false;
};

struct Evidence {
  Id id;
  Id hypothesis_id;
  EvidenceKind kind = EvidenceKind::Lexical;
  Label source;
  double score = 0.0;
  double confidence = 0.0;
  std::uint64_t valid_from_revision = 0;
  std::uint64_t valid_until_revision = 0;
  std::array<Id, kMaxDependenciesPerEvidence> dependency_ids{};
  std::uint8_t dependency_count = 0;
};

struct EvidencePolicy {
  std::array<double, 10> kind_weights{{1.0, 1.0, 1.0, 1.0, 1.0,
                                       1.0, 1.0, 1.0, 1.0, 1.0}};
};

struct Constraint {
  Id id;
  ConstraintKind kind = ConstraintKind::Semantic;
  Id subject_hypothesis;
  Id object_hypothesis;
  Label rule;
  bool hard = false;
  double weight = 0.0;
};

struct Decision {
  Id id;
  Id selected_hypothesis;
  std::array<Id, kMaxCandidatesPerSet> compared_hypotheses{};
  std::uint8_t compared_count = 0;
  std::array<Id, kMaxEvidencePerDecision> evidence_ids{};
  std::uint8_t evidence_count = 0;
  double confidence = 0.0;
  std::uint64_t revision = 0;
};

struct Revision {
  Id id;
  Id target_id;
  Id previous_hypothesis;
  Id replacement_hypothesis;
  std::uint64_t revision = 0;
  Label reason;
};

struct CommitSpan {
  Span span;
  Text output;
  std::uint64_t revision = 0;
  bool final = false;
};

class LanguageState {
 public:
  bool add_hypothesis(const Hypothesis& value);
  bool add_candidate_set(const CandidateSet& value);
  bool add_semantic_frame(const SemanticFrame& value);
  bool add_syntax_relation(const SyntaxRelation& value);
  bool add_orthographic_hypothesis(const OrthographicHypothesis& value);
  bool add_entity(const Entity& value);
  bool add_evidence(const Evidence& value);
  bool add_constraint(const Constraint& value);
  bool add_decision(const Decision& value);
  bool add_revision(const Revision& value);
  bool add_commit_span(const CommitSpan& value);

  const Hypothesis* find_hypothesis(std::string_view id) const;
  const Hypothesis* find_hypothesis_for_candidate(std::uint16_t start, std::uint16_t end,
                                                   std::string_view candidate_id,
                                                   std::string_view surface) const;
  const Entity* find_entity(std::string_view id) const;
  const Evidence* find_evidence(std::string_view id) const;
  bool aggregate_evidence(std::string_view hypothesis_id, std::uint64_t revision,
                         const EvidencePolicy& policy, double& score) const;
  std::size_t hypothesis_count() const { return hypothesis_count_; }
  std::size_t candidate_set_count() const { return candidate_set_count_; }
  std::size_t entity_count() const { return entity_count_; }
  std::size_t evidence_count() const { return evidence_count_; }
  std::size_t decision_count() const { return decision_count_; }
  std::size_t revision_count() const { return revision_count_; }
  std::size_t commit_span_count() const { return commit_span_count_; }
  std::size_t memory_bytes() const;

 private:
  template <typename T, std::size_t N>
  static bool has_id(const std::array<T, N>& values, std::size_t count, std::string_view id);
  std::array<Hypothesis, kMaxHypotheses> hypotheses_{};
  std::array<CandidateSet, kMaxCandidateSets> candidate_sets_{};
  std::array<SemanticFrame, kMaxFrames> frames_{};
  std::array<SyntaxRelation, kMaxRelations> relations_{};
  std::array<OrthographicHypothesis, kMaxHypotheses> orthography_{};
  std::array<Entity, kMaxEntities> entities_{};
  std::array<Evidence, kMaxEvidence> evidence_{};
  std::array<Constraint, kMaxConstraints> constraints_{};
  std::array<Decision, kMaxDecisions> decisions_{};
  std::array<Revision, kMaxRevisions> revisions_{};
  std::array<CommitSpan, kMaxCommitSpans> commits_{};
  std::size_t hypothesis_count_ = 0, candidate_set_count_ = 0, frame_count_ = 0;
  std::size_t relation_count_ = 0, orthography_count_ = 0, entity_count_ = 0;
  std::size_t evidence_count_ = 0, constraint_count_ = 0, decision_count_ = 0, revision_count_ = 0;
  std::size_t commit_span_count_ = 0;
};

struct PathRerankMetrics {
  std::size_t supported_paths = 0;
  bool order_changed = false;
  double best_evidence_score = 0.0;
};
bool rerank_paths(std::vector<::kana_decoder::DecodedPath>& paths,
                  const LanguageState& state, const EvidencePolicy& policy,
                  std::uint64_t revision, double evidence_weight,
                  PathRerankMetrics* metrics = nullptr);

enum class NumericStyle : std::uint8_t { Arabic, GroupedArabic };
enum class NumericEntityTarget : std::uint8_t { Number, Percent, Currency, Unit };

struct SurfaceRenderOptions {
  NumericStyle numeric_style = NumericStyle::GroupedArabic;
  DiscourseAct discourse_act = DiscourseAct::Statement;
  PunctuationStyle punctuation_style = PunctuationStyle::Japanese;
  bool normalize_numbers = true;
  bool append_terminal_punctuation = false;
  bool space_latin_boundaries = false;
};

class LanguageAdapter {
 public:
  virtual ~LanguageAdapter() = default;
  virtual std::string_view language_tag() const = 0;
  virtual bool make_hypothesis(const ::kana_decoder::Candidate& candidate,
                               Span span, std::string_view hypothesis_id,
                               Hypothesis& output) const = 0;
  virtual bool make_numeric_entity(std::string_view reading, std::string_view entity_id,
                                   NumericEntityTarget target, std::string_view unit,
                                   Entity& output) const = 0;
  virtual bool render_entity(const Entity& entity, NumericStyle style,
                             std::string& output) const = 0;
  virtual bool render_path(const ::kana_decoder::DecodedPath& path,
                           std::string_view reading,
                           const SurfaceRenderOptions& options,
                           std::string& output) const = 0;
};

}  // namespace kana_decoder::surge
