#pragma once

#include "kana_decoder/engine.hpp"
#include "kana_decoder/surge_language.hpp"
#include "surge/chaos_cluster.hpp"
#include "surge/context_graph.hpp"
#include "surge/event_journal.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace kana_decoder {

struct SurgeRuntimeConfig {
  std::string journal_path;
  std::string context_id{"kana-normalization"};
  std::size_t max_journal_events{65536};
  std::size_t max_journal_transactions{8192};
  std::size_t max_journal_payload_bytes{32U * 1024U * 1024U};
  std::size_t max_journal_identity_bytes{16U * 1024U * 1024U};
  std::size_t max_candidates_per_decode{64};
  std::map<std::string, double> recognition_family_weights;
  double normalization_renderer_score{1.0};
  double minimum_recognition_margin{0.0};
  const surge::LanguageAdapter* language_adapter{nullptr};
  surge::SurfaceRenderOptions surface_render_options{};
  bool remove_journal_on_destroy{false};
};

struct SurgeRuntimeMetrics {
  std::size_t claims{0};
  std::size_t evidence{0};
  std::size_t entities{0};
  std::size_t clusters{0};
  std::size_t contradictions{0};
  std::size_t questions{0};
  std::size_t chaos_members{0};
  std::size_t typed_entities_found{0};
  std::size_t typed_entity_evidence_added{0};
  std::size_t selected_typed_entity_evidence{0};
  std::size_t typed_entities_persisted{0};
  std::size_t focus_retrieved_claims{0};
  std::size_t focus_evidence_added{0};
  std::size_t dependency_triggered{0};
  std::size_t dependency_impacted_nodes{0};
  std::size_t retrieval_relations_scanned{0};
  bool retrieval_truncated{false};
  bool dependency_skipped_budget{false};
  std::uint64_t state_version{0};
};

struct SurgeNormalizationResult {
  bool committed{false};
  std::string output;
  std::string error;
  SurgeRuntimeMetrics metrics;
  std::vector<std::string> selected_claim_ids;
};

// The SURGE Core is the authoritative context, evidence, question, temporal,
// chaos-memory and commit journal. The kana decoder only supplies candidates
// and language-specific rendering.
class SurgeNormalizationRuntime {
 public:
  explicit SurgeNormalizationRuntime(SurgeRuntimeConfig config);
  ~SurgeNormalizationRuntime();

  SurgeNormalizationRuntime(const SurgeNormalizationRuntime&) = delete;
  SurgeNormalizationRuntime& operator=(const SurgeNormalizationRuntime&) = delete;

  bool open(std::string& error);
  bool set_language_adapter(const surge::LanguageAdapter* adapter);
  SurgeNormalizationResult normalize(const std::string& reading,
                                      const std::vector<DecodedPath>& paths,
                                      bool finalize = true);
  bool commit_output(const std::string& output, std::uint64_t source_revision,
                     bool final, std::string& error);

  bool reassess_claim(const std::string& claim_id, const std::string& reference_at,
                      ::surge::TemporalReassessmentRecord& record, std::string& error);
  std::uint64_t state_version() const;
  std::size_t graph_node_count() const;

 private:
  struct CandidateRecord;
  bool ensure_context_cluster(std::string& error);
  SurgeRuntimeConfig config_;
  std::unique_ptr<::surge::EventJournal> journal_;
  std::unique_ptr<::surge::ContextGraph> graph_;
  std::unique_ptr<::surge::ChaosClusterLedger> chaos_;
  mutable std::mutex mutex_;
  std::uint64_t sequence_{1};
  std::string context_cluster_id_;
  std::string chaos_cluster_id_;
  bool opened_{false};
};

}  // namespace kana_decoder
