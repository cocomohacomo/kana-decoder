#include "kana_decoder/surge_runtime.hpp"
#include "kana_decoder/surge_language.hpp"
#include "surge/dependency_engine.hpp"
#include "surge/question_router.hpp"
#include "surge/sha256.hpp"

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

std::string codepoint_slice(const std::string& input, std::size_t start, std::size_t end) {
  if (!surge::is_valid_utf8(input) || start >= end) return {};
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
  return surge::sha256_hex(value).substr(0, 20);
}

struct CandidateRecord {
  std::size_t start{0};
  std::size_t end{0};
  std::string key;
  std::string claim_id;
  std::string entity_id;
  std::string evidence_id;
  std::string dependency_id;
  std::string surface;
  std::string normalized_surface;
};

}  // namespace

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
      config_.max_candidates_per_decode == 0 || config_.max_candidates_per_decode > 64 ||
      config_.max_journal_events == 0 || config_.max_journal_transactions == 0 ||
      config_.max_journal_payload_bytes < 1024 || config_.max_journal_identity_bytes < 1024) {
    error = "SURGE_RUNTIME_CONFIG_INVALID";
    return false;
  }
  surge::EventJournalLimits limits;
  limits.max_events = config_.max_journal_events;
  limits.max_payload_bytes = config_.max_journal_payload_bytes;
  limits.max_transactions = config_.max_journal_transactions;
  limits.max_identity_bytes = config_.max_journal_identity_bytes;
  journal_ = std::make_unique<surge::EventJournal>(config_.journal_path, limits);
  graph_ = std::make_unique<surge::ContextGraph>(*journal_);
  chaos_ = std::make_unique<surge::ChaosClusterLedger>(*journal_);
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
  surge::SemanticDelta delta;
  delta.delta_id = "normalization/context-create/" + id_component(config_.context_id);
  delta.idempotency_key = delta.delta_id;
  delta.expected_state_version = journal_->state_version();
  delta.nodes.push_back({{context_cluster_id_, surge::NodeKind::Cluster,
      "Streaming kana-to-text normalization context; adapter=" + config_.context_id, {}}});
  const auto result = graph_->apply(delta);
  if (!result.committed) {
    error = "SURGE_CONTEXT_CLUSTER_CREATE_FAILED:" + result.error;
    return false;
  }
  return true;
}

SurgeNormalizationResult SurgeNormalizationRuntime::normalize(
    const std::string& reading, const std::vector<DecodedPath>& paths) {
  std::lock_guard<std::mutex> lock(mutex_);
  SurgeNormalizationResult result;
  if (!opened_ || !graph_ || !chaos_ || !journal_) {
    result.error = "SURGE_RUNTIME_NOT_OPEN";
    return result;
  }
  if (reading.empty() || !surge::is_valid_utf8(reading) || paths.empty() ||
      paths.size() > 8 || paths.front().tokens.empty()) {
    result.error = "SURGE_NORMALIZATION_INPUT_INVALID";
    return result;
  }

  const std::string timestamp = utc_now();
  const std::string source_digest = surge::sha256_hex(reading);
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
      record.evidence_id = prefix + "/evidence/" + suffix;
      record.dependency_id = prefix + "/dependency/" + suffix;

      const auto token_reading = codepoint_slice(reading, token.start, token.end);
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
          entity.content = "numeric_entity;raw=" + token_reading +
              ";normalized=" + record.normalized_surface +
              ";mantissa=" + std::to_string(parsed.mantissa) +
              ";decimal_places=" + std::to_string(parsed.decimal_places);
        }
      }
      candidates.emplace(key, std::move(record));
    }
  }

  surge::SemanticDelta delta;
  delta.delta_id = prefix + "/semantic-delta";
  delta.idempotency_key = prefix + "/semantic-idempotency";
  delta.expected_state_version = journal_->state_version();

  for (const auto& item : candidates) {
    const auto& record = item.second;
    const auto token_reading = codepoint_slice(reading, record.start, record.end);
    const auto& token = [&]() -> const DecodedToken& {
      for (const auto& path : paths)
        for (const auto& candidate : path.tokens)
          if (candidate_key(candidate) == record.key) return candidate;
      return paths.front().tokens.front();
    }();
    std::ostringstream evidence_content;
    evidence_content << "producer=kana-decoder;candidate=" << token.candidate.id
                     << ";surface=" << token.candidate.surface
                     << ";pos=" << token.candidate.pos
                     << ";reading=" << token_reading
                     << ";morphology_score=" << std::setprecision(17) << token.morphology_score
                     << ";context_score=" << token.context_score
                     << ";source_digest=" << source_digest
                     << ";observed_at=" << timestamp;
    delta.nodes.push_back({{record.entity_id, surge::NodeKind::Entity,
        record.normalized_surface.empty() ? token.candidate.surface : record.normalized_surface, {}}});
    delta.nodes.push_back({{record.evidence_id, surge::NodeKind::Evidence,
        evidence_content.str(), {}}});
    delta.nodes.push_back({{record.claim_id, surge::NodeKind::Claim,
        "candidate interpretation: " + token.candidate.surface + " [" + token.candidate.pos + "]",
        {record.evidence_id}}});
    delta.nodes.push_back({{record.dependency_id, surge::NodeKind::Dependency,
        "CLAIM_TEMPORAL_STATE:VALID@" + timestamp,
        {record.claim_id, record.entity_id}}});
    delta.relations.push_back({{prefix + "/supports/" + id_component(record.claim_id),
        surge::RelationKind::Supports, record.evidence_id, record.claim_id}});
    delta.relations.push_back({{prefix + "/about-cluster/" + id_component(record.claim_id),
        surge::RelationKind::About, record.claim_id, context_cluster_id_}});
    delta.relations.push_back({{prefix + "/about-entity/" + id_component(record.claim_id),
        surge::RelationKind::About, record.claim_id, record.entity_id}});

    surge::ClaimValidity claim_validity;
    claim_validity.claim_id = record.claim_id;
    claim_validity.valid_from = timestamp;
    claim_validity.basis_evidence_id = record.evidence_id;
    claim_validity.revision = 1;
    delta.claim_validity_updates.push_back(claim_validity);

    surge::EvidenceValidity evidence_validity;
    evidence_validity.evidence_id = record.evidence_id;
    evidence_validity.valid_from = timestamp;
    evidence_validity.basis_evidence_id = record.evidence_id;
    evidence_validity.revision = 1;
    delta.evidence_validity_updates.push_back(evidence_validity);
  }

  const auto applied = graph_->apply(delta);
  if (!applied.committed) {
    result.error = "SURGE_SEMANTIC_DELTA_REJECTED:" + applied.error;
    return result;
  }
  result.metrics.claims = candidates.size();
  result.metrics.evidence = candidates.size();
  result.metrics.entities = candidates.size();

  std::vector<std::pair<std::string, std::string>> contradictions;
  for (auto left = candidates.begin(); left != candidates.end(); ++left) {
    if (contradictions.size() >= 16) break;
    auto right = left;
    ++right;
    for (; right != candidates.end(); ++right) {
      if (left->second.start == right->second.start &&
          left->second.end == right->second.end &&
          left->second.surface != right->second.surface) {
        const auto relation_id = prefix + "/contradicts/" +
            id_component(left->second.claim_id + right->second.claim_id);
        surge::SemanticDelta conflict;
        conflict.delta_id = relation_id + "/delta";
        conflict.idempotency_key = relation_id + "/key";
        conflict.expected_state_version = journal_->state_version();
        conflict.relations.push_back({{relation_id, surge::RelationKind::Contradicts,
            left->second.claim_id, right->second.claim_id}});
        const auto conflict_result = graph_->apply(conflict);
        if (!conflict_result.committed) {
          result.error = "SURGE_CONTRADICTION_REJECTED:" + conflict_result.error;
          return result;
        }
        contradictions.emplace_back(left->second.claim_id, right->second.claim_id);
        ++result.metrics.contradictions;
        if (contradictions.size() >= 16) break;
      }
    }
  }

  if (!contradictions.empty()) {
    for (const auto& relation : graph_->relations()) {
      if (relation.second.kind != surge::RelationKind::Contradicts ||
          relation.first.find(prefix + "/contradicts/") != 0) continue;
      const auto routed = surge::QuestionRouter::route(*graph_, relation.first, "kana-normalization-v1");
      if (!routed.committed) {
        result.error = "SURGE_QUESTION_ROUTING_FAILED:" + routed.error;
        return result;
      }
      if (!routed.duplicate) ++result.metrics.questions;
    }
  }

  std::set<std::string> best_path_keys(path_candidate_keys.front().begin(),
                                       path_candidate_keys.front().end());
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
  if (alternatives.size() > 16) alternatives.resize(16);

  surge::ChaosProvenance provenance{"kana-decoder", "1", "normalization-v1", timestamp};
  if (!alternatives.empty()) {
    surge::ChaosCluster existing_chaos;
    if (!chaos_->get(chaos_cluster_id_, existing_chaos)) {
      const auto created = chaos_->create(prefix + "/chaos-create",
          prefix + "/chaos-create-key", journal_->state_version(), chaos_cluster_id_, provenance);
      if (!created.committed) {
        result.error = "SURGE_CHAOS_CLUSTER_CREATE_FAILED:" + created.error;
        return result;
      }
    }
    // Keep both the selected and unresolved alternatives in the same open-world
    // exploration space so provisional contradiction edges have two members.
    for (const auto& item : candidates) {
      surge::ChaosMember member;
      member.member_ref = item.second.claim_id;
      member.event_ref = item.second.evidence_id;
      member.admission_reason = "retained N-best normalization hypothesis";
      member.admitted_at = timestamp;
      member.source_digest = source_digest;
      member.provenance = provenance;
      const auto admitted = chaos_->admit_member(prefix + "/chaos-admit/" +
          id_component(item.second.claim_id), prefix + "/chaos-admit-key/" +
          id_component(item.second.claim_id), journal_->state_version(),
          chaos_cluster_id_, member);
      if (!admitted.committed) {
        result.error = "SURGE_CHAOS_MEMBER_ADMISSION_FAILED:" + admitted.error;
        return result;
      }
      ++result.metrics.chaos_members;
    }
  }

  for (const auto& conflict : contradictions) {
    const auto association_id = prefix + "/chaos-association/" +
        id_component(conflict.first + conflict.second);
    surge::ChaosAssociation association;
    association.association_id = association_id;
    association.from_ref = conflict.first;
    association.to_ref = conflict.second;
    association.type = surge::ChaosAssociationType::Contradiction;
    association.status = surge::ChaosAssociationStatus::Provisional;
    association.provenance = provenance;
    const auto proposed = chaos_->propose_association(association_id + "/op",
        association_id + "/key", journal_->state_version(), chaos_cluster_id_, association);
    if (!proposed.committed && proposed.error != "CHAOS_MEMBER_NOT_FOUND") {
      result.error = "SURGE_CHAOS_ASSOCIATION_FAILED:" + proposed.error;
      return result;
    }
  }

  const auto best_claims = path_candidate_keys.front();
  for (const auto& key : best_claims) {
    const auto found = candidates.find(key);
    if (found == candidates.end() || graph_->claim_status(found->second.claim_id) != surge::ClaimStatus::Active) {
      result.error = "SURGE_SELECTED_CLAIM_NOT_ACTIVE";
      return result;
    }
    surge::TemporalReassessmentRecord reassessment;
    std::string reassessment_error;
    if (!graph_->reassess_claim(found->second.claim_id, timestamp,
                                journal_->state_version(), reassessment, reassessment_error)) {
      result.error = "SURGE_TEMPORAL_REASSESSMENT_FAILED:" + reassessment_error;
      return result;
    }
  }

  const auto& selected_path = paths.front();
  std::ostringstream commit_payload;
  commit_payload << "context=" << config_.context_id
                 << ";sequence=" << current_sequence
                 << ";reading_sha256=" << source_digest
                 << ";selected_claims=";
  for (std::size_t i = 0; i < best_claims.size(); ++i) {
    if (i) commit_payload << ",";
    commit_payload << candidates.at(best_claims[i]).claim_id;
  }
  commit_payload << ";output=";
  std::string output;
  for (const auto& token : selected_path.tokens) {
    const auto found = candidates.find(candidate_key(token));
    if (found == candidates.end()) {
      result.error = "SURGE_SELECTED_HYPOTHESIS_NOT_FOUND";
      return result;
    }
    output += found->second.normalized_surface.empty()
        ? token.candidate.surface : found->second.normalized_surface;
  }
  commit_payload << output;
  const auto decision_id = prefix + "/decision";
  const auto commit_id = prefix + "/commit";
  const auto journal_result = journal_->commit(prefix + "/output-transaction",
      prefix + "/output-idempotency", journal_->state_version(),
      {{decision_id, "NORMALIZATION_DECISION", commit_payload.str()},
       {commit_id, "NORMALIZATION_COMMIT", commit_payload.str()}});
  if (!journal_result.committed) {
    result.error = "SURGE_OUTPUT_COMMIT_FAILED:" + journal_result.error;
    return result;
  }

  result.committed = true;
  result.output = std::move(output);
  result.metrics.state_version = journal_result.state_version;
  return result;
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
