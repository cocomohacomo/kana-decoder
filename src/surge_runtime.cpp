#include "kana_decoder/surge_runtime.hpp"
#include "kana_decoder/surge_language.hpp"
#include "surge/dependency_engine.hpp"
#include "surge/question_router.hpp"
#include "surge/sha256.hpp"
#include "surge/decision.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <map>
#include <filesystem>
#include <set>
#include <sstream>
#include <utility>

namespace kana_decoder {
namespace {

std::string utc_now() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t value = std::chrono::system_clock::to_time_t(now);
  std::tm parts{};
#if defined(_WIN32)
  if (gmtime_s(&parts, &value) != 0) return "1970-01-01T00:00:00Z";
#else
  if (gmtime_r(&value, &parts) == nullptr) return "1970-01-01T00:00:00Z";
#endif
  char buffer[21]{};
  if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &parts) == 0)
    return "1970-01-01T00:00:00Z";
  return buffer;
}

std::size_t utf8_codepoint_count(const std::string& input) {
  if (!::surge::is_valid_utf8(input)) return 0;
  std::size_t count = 0;
  for (std::size_t i = 0; i < input.size();) {
    const auto c = static_cast<unsigned char>(input[i]);
    i += c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2 :
         (c & 0xf0) == 0xe0 ? 3 : 4;
    ++count;
  }
  return count;
}

std::string codepoint_slice(const std::string& input, std::size_t start, std::size_t end) {
  if (!::surge::is_valid_utf8(input) || start >= end) return {};
  std::size_t cp = 0, byte_start = std::string::npos, byte_end = std::string::npos;
  for (std::size_t i = 0; i < input.size();) {
    if (cp == start) byte_start = i;
    if (cp == end) { byte_end = i; break; }
    const auto c = static_cast<unsigned char>(input[i]);
    const std::size_t width = c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2 :
                              (c & 0xf0) == 0xe0 ? 3 : 4;
    i += width;
    ++cp;
  }
  if (byte_start == std::string::npos) return {};
  if (byte_end == std::string::npos && cp == end) byte_end = input.size();
  if (byte_end == std::string::npos || byte_end <= byte_start) return {};
  return input.substr(byte_start, byte_end - byte_start);
}

std::string candidate_key(const DecodedToken& token) {
  return std::to_string(token.start) + ":" + std::to_string(token.end) + ":" +
         token.candidate.id + ":" + token.candidate.surface;
}

std::string id_component(const std::string& value) {
  return ::surge::sha256_hex(value).substr(0, 20);
}

}  // namespace

struct SurgeNormalizationRuntime::CandidateRecord {
  std::size_t start{0};
  std::size_t end{0};
  std::string key;
  std::string claim_id;
  std::string entity_id;
  std::vector<std::string> evidence_ids;
  std::string dependency_id;
  std::string surface;
  std::string normalized_surface;
  std::string entity_content;
};

SurgeNormalizationRuntime::SurgeNormalizationRuntime(SurgeRuntimeConfig config)
    : config_(std::move(config)) {
  context_cluster_id_ = "cluster/normalization/" + id_component(config_.context_id);
  chaos_cluster_id_ = "chaos/normalization/" + id_component(config_.context_id);
}

SurgeNormalizationRuntime::~SurgeNormalizationRuntime() {
  chaos_.reset();
  graph_.reset();
  journal_.reset();
  if (config_.remove_journal_on_destroy && !config_.journal_path.empty()) {
    std::error_code ignored;
    std::filesystem::remove(config_.journal_path, ignored);
  }
}

bool SurgeNormalizationRuntime::open(std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (opened_) return true;
  if (config_.journal_path.empty() || config_.context_id.empty() ||
      config_.context_id.size() > 128 || config_.max_candidates_per_decode == 0 || config_.max_candidates_per_decode > 64 ||
      config_.max_journal_events == 0 || config_.max_journal_transactions == 0 ||
      config_.max_journal_payload_bytes < 1024 || config_.max_journal_identity_bytes < 1024) {
    error = "SURGE_RUNTIME_CONFIG_INVALID";
    return false;
  }
  ::surge::EventJournalLimits limits;
  limits.max_events = config_.max_journal_events;
  limits.max_payload_bytes = config_.max_journal_payload_bytes;
  limits.max_transactions = config_.max_journal_transactions;
  limits.max_identity_bytes = config_.max_journal_identity_bytes;
  journal_ = std::make_unique<::surge::EventJournal>(config_.journal_path, limits);
  graph_ = std::make_unique<::surge::ContextGraph>(*journal_);
  chaos_ = std::make_unique<::surge::ChaosClusterLedger>(*journal_);
  if (!graph_->open()) {
    error = "SURGE_CONTEXT_GRAPH_OPEN_FAILED:" + journal_->last_error();
    return false;
  }
  if (!chaos_->open()) {
    error = "SURGE_CHAOS_LEDGER_OPEN_FAILED:" + journal_->last_error();
    return false;
  }
  sequence_ = journal_->state_version() + 1;
  if (!ensure_context_cluster(error)) return false;
  opened_ = true;
  return true;
}

bool SurgeNormalizationRuntime::ensure_context_cluster(std::string& error) {
  if (graph_->nodes().count(context_cluster_id_) != 0) return true;
  ::surge::SemanticDelta delta;
  delta.delta_id = "normalization/context-create/" + id_component(config_.context_id);
  delta.idempotency_key = delta.delta_id;
  delta.expected_state_version = journal_->state_version();
  delta.nodes.push_back({{context_cluster_id_, ::surge::NodeKind::Cluster,
      "Streaming kana-to-text normalization context; adapter=" + config_.context_id, {}}});
  const auto result = graph_->apply(delta);
  if (!result.committed) {
    error = "SURGE_CONTEXT_CLUSTER_CREATE_FAILED:" + result.error;
    return false;
  }
  return true;
}

SurgeNormalizationResult SurgeNormalizationRuntime::normalize(
    const std::string& reading, const std::vector<DecodedPath>& paths, bool finalize) {
  std::lock_guard<std::mutex> lock(mutex_);
  SurgeNormalizationResult result;
  if (!opened_ || !graph_ || !chaos_ || !journal_) {
    result.error = "SURGE_RUNTIME_NOT_OPEN";
    return result;
  }
  if (reading.empty() || !::surge::is_valid_utf8(reading) ||
      utf8_codepoint_count(reading) > 256 || paths.empty() ||
      paths.size() > 8 || paths.front().tokens.empty()) {
    result.error = "SURGE_NORMALIZATION_INPUT_INVALID";
    return result;
  }

  const std::string timestamp = utc_now();
  const std::string source_digest = ::surge::sha256_hex(reading);
  const std::uint64_t current_sequence = sequence_++;
  const std::string prefix = "norm/" + id_component(config_.context_id) + "/" +
                             std::to_string(current_sequence);
  std::map<std::string, CandidateRecord> candidates;
  std::vector<std::vector<std::string>> path_candidate_keys(paths.size());

  for (std::size_t p = 0; p < paths.size(); ++p) {
    if (paths[p].tokens.empty()) continue;
    for (const auto& token : paths[p].tokens) {
      if (token.start >= token.end || token.end > 256 || token.candidate.id.empty() ||
          token.candidate.surface.empty()) {
        result.error = "SURGE_CANDIDATE_INVALID";
        return result;
      }
      const auto key = candidate_key(token);
      path_candidate_keys[p].push_back(key);
      if (candidates.count(key) != 0) continue;
      if (candidates.size() >= config_.max_candidates_per_decode) {
        result.error = "SURGE_CANDIDATE_BUDGET_EXCEEDED";
        return result;
      }
      CandidateRecord record;
      record.start = token.start;
      record.end = token.end;
      record.key = key;
      record.surface = token.candidate.surface;
      const auto suffix = id_component(key);
      record.claim_id = prefix + "/claim/" + suffix;
      record.entity_id = prefix + "/entity/" + suffix;
      record.dependency_id = prefix + "/dependency/" + suffix;

      const auto token_reading = codepoint_slice(reading, token.start, token.end);
      if (token_reading.empty()) {
        result.error = "SURGE_CANDIDATE_SPAN_OUT_OF_RANGE";
        return result;
      }
      kana_decoder::surge::ParsedNumber parsed;
      if (kana_decoder::surge::parse_japanese_number(token_reading, parsed)) {
        kana_decoder::surge::Entity numeric;
        numeric.id.assign("number-" + suffix);
        numeric.raw.assign(token_reading);
        numeric.kind = kana_decoder::surge::EntityKind::Number;
        numeric.value = parsed.mantissa;
        numeric.decimal_places = parsed.decimal_places;
        numeric.valid = true;
        if (kana_decoder::surge::render_entity(numeric,
              kana_decoder::surge::NumericStyle::GroupedArabic, record.normalized_surface)) {
          record.entity_content = "numeric;raw=" + token_reading +
              ";mantissa=" + std::to_string(parsed.mantissa) +
              ";decimal_places=" + std::to_string(parsed.decimal_places) +
              ";normalized=" + record.normalized_surface;
        }
      }
      candidates.emplace(key, std::move(record));
    }
  }

  std::vector<::surge::DecisionCandidate> recognition_candidates;
  recognition_candidates.reserve(paths.size());
  for (std::size_t p = 0; p < paths.size(); ++p) {
    if (paths[p].tokens.empty()) continue;
    ::surge::DecisionCandidate candidate;
    candidate.candidate_id = "path-" + std::to_string(p);
    candidate.stable_order = p;
    candidate.evidence.push_back({"decoder-path-" + std::to_string(p),
        "decoder_prior", "decoder_path", paths[p].score, 0.05, true});
    for (std::size_t t = 0; t < paths[p].tokens.size(); ++t) {
      const auto& token = paths[p].tokens[t];
      const std::string span = std::to_string(token.start) + ":" + std::to_string(token.end);
      const std::string prefix_id = "p" + std::to_string(p) + "-t" + std::to_string(t) + "-";
      candidate.evidence.push_back({prefix_id + "lexical", "lexical", span,
          token.lexical_score, 0.25, true});
      candidate.evidence.push_back({prefix_id + "morphology", "morphology", span,
          token.morphology_score, 0.10, true});
      if (token.has_transition_evidence)
        candidate.evidence.push_back({prefix_id + "transition", "transition", span,
            token.transition_score, 0.20, true});
      if (token.has_ngram_evidence)
        candidate.evidence.push_back({prefix_id + "ngram", "ngram", span,
            token.ngram_score, 0.35, true});
      if (token.has_pos_evidence)
        candidate.evidence.push_back({prefix_id + "pos", "pos_ngram", span,
            token.pos_score, 0.20, true});
      if (token.has_cluster_evidence)
        candidate.evidence.push_back({prefix_id + "cluster", "context_cluster", span,
            token.cluster_raw_evidence, 0.50, true});
    }
    recognition_candidates.push_back(std::move(candidate));
  }
  ::surge::DecisionPolicy recognition_policy;
  recognition_policy.max_candidates = config_.max_candidates_per_decode;
  recognition_policy.max_evidence_per_candidate = 2048;
  recognition_policy.minimum_margin = 0.0;
  const auto recognition = ::surge::RecognitionDecision::evaluate(
      recognition_candidates, recognition_policy);
  if (recognition.disposition != ::surge::DecisionDisposition::Select) {
    result.error = "SURGE_RECOGNITION_DEFERRED:" + recognition.reason;
    return result;
  }
  const std::string selected_path_name = recognition.selected_candidate_id;
  if (selected_path_name.rfind("path-", 0) != 0) {
    result.error = "SURGE_RECOGNITION_RESULT_INVALID";
    return result;
  }
  const std::size_t selected_path_index =
      static_cast<std::size_t>(std::stoul(selected_path_name.substr(5)));
  if (selected_path_index >= paths.size() || path_candidate_keys[selected_path_index].empty()) {
    result.error = "SURGE_RECOGNITION_PATH_NOT_FOUND";
    return result;
  }
  const auto best_claims = path_candidate_keys[selected_path_index];
  std::set<std::string> best_path_keys(best_claims.begin(), best_claims.end());
  std::vector<const CandidateRecord*> alternatives;
  for (const auto& item : candidates) {
    if (best_path_keys.count(item.first) == 0) alternatives.push_back(&item.second);
  }
  std::sort(alternatives.begin(), alternatives.end(),
      [](const CandidateRecord* a, const CandidateRecord* b) {
        if (a->start != b->start) return a->start < b->start;
        if (a->end != b->end) return a->end < b->end;
        return a->claim_id < b->claim_id;
      });
  if (alternatives.size() > 16) {
    result.error = "SURGE_CHAOS_ALTERNATIVE_BUDGET_EXCEEDED";
    return result;
  }

  std::string output;
  std::vector<std::string> normalization_trace;
  for (const auto& token : paths[selected_path_index].tokens) {
    const auto key = candidate_key(token);
    const auto found = candidates.find(key);
    if (found == candidates.end()) {
      result.error = "SURGE_SELECTED_HYPOTHESIS_NOT_FOUND";
      return result;
    }
    ::surge::DecisionCandidate original_surface;
    original_surface.candidate_id = "surface-original";
    original_surface.stable_order = 0;
    original_surface.evidence.push_back({"surface-original-evidence", "source_surface",
        "source", 0.0, 1.0, true});
    std::vector<::surge::DecisionCandidate> surface_hypotheses;
    surface_hypotheses.push_back(original_surface);
    if (!found->second.normalized_surface.empty() &&
        found->second.normalized_surface != token.candidate.surface) {
      ::surge::DecisionCandidate rendered_surface;
      rendered_surface.candidate_id = "surface-rendered";
      rendered_surface.stable_order = 1;
      rendered_surface.evidence.push_back({"surface-rendered-evidence", "language_renderer",
          "rendered", 1.0, 1.0, true});
      surface_hypotheses.push_back(std::move(rendered_surface));
    }
    ::surge::DecisionPolicy normalization_policy;
    normalization_policy.max_candidates = 2;
    normalization_policy.max_evidence_per_candidate = 4;
    normalization_policy.minimum_margin = 0.0;
    const auto normalization = ::surge::NormalizationDecision::evaluate(
        surface_hypotheses, normalization_policy);
    if (normalization.disposition != ::surge::DecisionDisposition::Select) {
      result.error = "SURGE_NORMALIZATION_DEFERRED:" + normalization.reason;
      return result;
    }
    const bool use_rendered = normalization.selected_candidate_id == "surface-rendered";
    const std::string surface = use_rendered
        ? found->second.normalized_surface : token.candidate.surface;
    normalization_trace.push_back(key + "=" + normalization.selected_candidate_id);
    output += surface;
  }
  std::ostringstream commit_payload;
  commit_payload << "context=" << config_.context_id
                 << ";sequence=" << current_sequence
                 << ";reading_sha256=" << source_digest
                 << ";selected_claims=";
  for (std::size_t i = 0; i < best_claims.size(); ++i) {
    if (i) commit_payload << ",";
    commit_payload << candidates.at(best_claims[i]).claim_id;
  }
  commit_payload << ";normalization_decisions=";
  for (std::size_t i = 0; i < normalization_trace.size(); ++i) {
    if (i) commit_payload << ",";
    commit_payload << normalization_trace[i];
  }
  commit_payload << ";output=" << output;

  std::vector<std::string> contradiction_relation_ids;
  std::size_t contradiction_count = 0;
  ::surge::SemanticDelta delta;
  delta.delta_id = prefix + "/semantic-delta";
  delta.idempotency_key = prefix + "/semantic-idempotency";
  delta.expected_state_version = journal_->state_version();

  for (auto& item : candidates) {
    auto& record = item.second;
    const auto token_reading = codepoint_slice(reading, record.start, record.end);
    const auto& token = [&]() -> const DecodedToken& {
      for (const auto& path : paths)
        for (const auto& candidate : path.tokens)
          if (candidate_key(candidate) == record.key) return candidate;
      return paths[selected_path_index].tokens.front();
    }();
    delta.nodes.push_back({{record.entity_id, ::surge::NodeKind::Entity,
        record.entity_content.empty() ? "lexical;surface=" + token.candidate.surface +
            ";pos=" + token.candidate.pos + ";inflection_type=" +
            token.candidate.inflection_type + ";inflection_form=" +
            token.candidate.inflection_form + ";reading=" + token_reading :
            record.entity_content, {}}});

    const std::string evidence_prefix = record.claim_id + "/evidence/";
    auto add_evidence = [&](const std::string& source, double score,
                            const std::string& detail) {
      const auto evidence_id = evidence_prefix + source;
      std::ostringstream content;
      content << "producer=kana-decoder;source=" << source
              << ";candidate=" << token.candidate.id
              << ";surface=" << token.candidate.surface
              << ";pos=" << token.candidate.pos
              << ";inflection_type=" << token.candidate.inflection_type
              << ";inflection_form=" << token.candidate.inflection_form
              << ";reading=" << token_reading
              << ";score=" << std::setprecision(17) << score
              << ";detail=" << detail
              << ";source_digest=" << source_digest
              << ";observed_at=" << timestamp;
      delta.nodes.push_back({{evidence_id, ::surge::NodeKind::Evidence,
          content.str(), {}}});
      delta.relations.push_back({{prefix + "/supports/" +
          id_component(record.claim_id + evidence_id),
          ::surge::RelationKind::Supports, evidence_id, record.claim_id}});
      ::surge::EvidenceValidity evidence_validity;
      evidence_validity.evidence_id = evidence_id;
      evidence_validity.valid_from = timestamp;
      evidence_validity.basis_evidence_id = evidence_id;
      evidence_validity.revision = 1;
      delta.evidence_validity_updates.push_back(evidence_validity);
      record.evidence_ids.push_back(evidence_id);
    };

    add_evidence("LEXICAL", token.lexical_score,
        "dictionary word/connection cost;aggregate_morphology=" +
        std::to_string(token.morphology_score));
    if (token.has_transition_evidence)
      add_evidence("TRANSITION", token.transition_score, "connection-matrix transition");
    if (token.has_ngram_evidence)
      add_evidence("WORD_NGRAM", token.ngram_score, "observed word n-gram context");
    if (token.has_pos_evidence)
      add_evidence("POS_NGRAM", token.pos_score, "observed POS n-gram context");
    if (token.has_cluster_evidence)
      add_evidence("SURGE_CLUSTER", token.cluster_raw_evidence,
          "cluster evidence;applied_bonus=" + std::to_string(token.cluster_score));

    if (record.evidence_ids.empty()) {
      result.error = "SURGE_HYPOTHESIS_WITHOUT_EVIDENCE";
      return result;
    }
    delta.nodes.push_back({{record.claim_id, ::surge::NodeKind::Claim,
        "candidate interpretation: " + token.candidate.surface + " [" + token.candidate.pos +
            ";inflection=" + token.candidate.inflection_type + "/" +
            token.candidate.inflection_form + "]",
        record.evidence_ids}});
    if (!best_claims.empty() && item.first == best_claims.front()) {
      delta.nodes.push_back({{record.dependency_id, ::surge::NodeKind::Dependency,
          "CLAIM_TEMPORAL_STATE:VALID@" + timestamp,
          {record.claim_id, record.entity_id}}});
    }
    delta.relations.push_back({{prefix + "/about-cluster/" + id_component(record.claim_id),
        ::surge::RelationKind::About, record.claim_id, context_cluster_id_}});
    delta.relations.push_back({{prefix + "/about-entity/" + id_component(record.claim_id),
        ::surge::RelationKind::About, record.claim_id, record.entity_id}});

    ::surge::ClaimValidity claim_validity;
    claim_validity.claim_id = record.claim_id;
    claim_validity.valid_from = timestamp;
    claim_validity.basis_evidence_id = record.evidence_ids.front();
    claim_validity.revision = 1;
    delta.claim_validity_updates.push_back(claim_validity);
  }

  for (auto left = candidates.begin(); left != candidates.end(); ++left) {
    auto right = left;
    ++right;
    for (; right != candidates.end(); ++right) {
      if (left->second.start == right->second.start &&
          left->second.end == right->second.end &&
          left->second.surface != right->second.surface) {
        if (contradiction_count >= 16) {
          result.error = "SURGE_CONTRADICTION_BUDGET_EXCEEDED";
          return result;
        }
        const auto relation_id = prefix + "/contradicts/" +
            id_component(left->second.claim_id + right->second.claim_id);
        delta.relations.push_back({{relation_id, ::surge::RelationKind::Contradicts,
            left->second.claim_id, right->second.claim_id}});
        contradiction_relation_ids.push_back(relation_id);
        ++contradiction_count;
      }
    }
  }

  const bool ambiguous = !alternatives.empty() || !contradiction_relation_ids.empty();
  const auto output_event_id = prefix + (finalize && !ambiguous ? "/commit" : "/proposal");
  delta.journal_events.push_back({output_event_id,
      finalize && !ambiguous ? "NORMALIZATION_COMMIT" : "NORMALIZATION_PROPOSAL",
      commit_payload.str()});

  const auto applied = graph_->apply(delta);
  if (!applied.committed) {
    result.error = "SURGE_SEMANTIC_DELTA_REJECTED:" + applied.error;
    return result;
  }
  result.metrics.claims = candidates.size();
  result.metrics.evidence = 0;
  for (const auto& item : candidates) result.metrics.evidence += item.second.evidence_ids.size();
  result.metrics.entities = candidates.size();
  result.metrics.contradictions = contradiction_count;

  for (const auto& relation_id : contradiction_relation_ids) {
    const auto routed = ::surge::QuestionRouter::route(*graph_, relation_id, "kana-normalization-v1");
    if (!routed.committed) {
      result.error = "SURGE_QUESTION_ROUTING_FAILED:" + routed.error;
      return result;
    }
    if (!routed.duplicate) ++result.metrics.questions;
  }

  ::surge::ChaosProvenance provenance{"kana-decoder", "1", "normalization-v1", timestamp};
  if (!alternatives.empty()) {
    ::surge::ChaosCluster existing_chaos;
    if (!chaos_->get(chaos_cluster_id_, existing_chaos)) {
      const auto created = chaos_->create(prefix + "/chaos-create",
          prefix + "/chaos-create-key", journal_->state_version(), chaos_cluster_id_, provenance);
      if (!created.committed) {
        result.error = "SURGE_CHAOS_CLUSTER_CREATE_FAILED:" + created.error;
        return result;
      }
    }
    const auto member_ref = prefix + "/candidate-set";
    ::surge::ChaosMember member;
    member.member_ref = member_ref;
    member.event_ref = candidates.begin()->second.evidence_ids.front();
    member.admission_reason = "open-world unresolved N-best candidate set";
    member.admitted_at = timestamp;
    member.source_digest = source_digest;
    member.provenance = provenance;
    const auto admitted = chaos_->admit_member(prefix + "/chaos-admit",
        prefix + "/chaos-admit-key", journal_->state_version(), chaos_cluster_id_, member);
    if (!admitted.committed) {
      result.error = "SURGE_CHAOS_MEMBER_ADMISSION_FAILED:" + admitted.error;
      return result;
    }
    ::surge::ChaosPatternHypothesis pattern;
    pattern.hypothesis_id = prefix + "/candidate-pattern";
    pattern.statement = "Competing normalization hypotheses for source digest " + source_digest;
    pattern.epistemic_state = "HYPOTHETICAL";
    pattern.status = "PROVISIONAL";
    pattern.member_refs.push_back(member_ref);
    for (const auto* alternative : alternatives)
      pattern.alternative_hypothesis_refs.push_back(alternative->claim_id);
    pattern.provenance = provenance;
    const auto recorded = chaos_->add_pattern_hypothesis(prefix + "/chaos-pattern",
        prefix + "/chaos-pattern-key", journal_->state_version(), chaos_cluster_id_, pattern);
    if (!recorded.committed) {
      result.error = "SURGE_CHAOS_PATTERN_FAILED:" + recorded.error;
      return result;
    }
    result.metrics.chaos_members = 1;
  }

  for (const auto& key : best_claims) {
    const auto found = candidates.find(key);
    if (found == candidates.end() ||
        graph_->claim_status(found->second.claim_id) != ::surge::ClaimStatus::Active) {
      result.error = "SURGE_SELECTED_CLAIM_NOT_ACTIVE";
      return result;
    }
  }

  if (finalize && ambiguous) {
    std::string commit_error;
    if (!commit_output(output, journal_->state_version(), true, commit_error)) {
      result.error = "SURGE_OUTPUT_COMMIT_FAILED:" + commit_error;
      return result;
    }
  }

  result.committed = true;
  result.output = std::move(output);
  for (const auto& key : best_claims)
    result.selected_claim_ids.push_back(candidates.at(key).claim_id);
  result.metrics.state_version = journal_->state_version();
  return result;
}

bool SurgeNormalizationRuntime::commit_output(const std::string& output,
    std::uint64_t source_revision, bool final, std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!opened_ || !journal_ || output.empty()) {
    error = "SURGE_OUTPUT_COMMIT_REQUEST_INVALID";
    return false;
  }
  const auto sequence = sequence_++;
  const auto id = "norm/" + id_component(config_.context_id) + "/" +
      std::to_string(sequence) + "/stream-commit";
  std::ostringstream payload;
  payload << "context=" << config_.context_id << ";source_revision=" << source_revision
          << ";final=" << (final ? "true" : "false") << ";output=" << output;
  const auto committed = journal_->commit(id, id + "/key", journal_->state_version(),
      {{id, final ? "NORMALIZATION_STREAM_FINAL_COMMIT" :
                    "NORMALIZATION_STREAM_PREFIX_COMMIT", payload.str()}});
  if (!committed.committed) {
    error = "SURGE_STREAM_COMMIT_FAILED:" + committed.error;
    return false;
  }
  return true;
}

bool SurgeNormalizationRuntime::reassess_claim(const std::string& claim_id,
    const std::string& reference_at, ::surge::TemporalReassessmentRecord& record,
    std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!opened_ || !graph_ || !journal_ || claim_id.empty() || reference_at.empty()) {
    error = "SURGE_REASSESSMENT_REQUEST_INVALID";
    return false;
  }
  return graph_->reassess_claim(claim_id, reference_at, journal_->state_version(),
                                record, error);
}

std::uint64_t SurgeNormalizationRuntime::state_version() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return journal_ ? journal_->state_version() : 0;
}

std::size_t SurgeNormalizationRuntime::graph_node_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return graph_ ? graph_->nodes().size() : 0;
}

}  // namespace kana_decoder
