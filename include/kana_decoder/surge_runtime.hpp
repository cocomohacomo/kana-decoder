#pragma once

#include "kana_decoder/engine.hpp"
#include "surge/chaos_cluster.hpp"
#include "surge/context_graph.hpp"
#include "surge/event_journal.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace kana_decoder {

struct SurgeRuntimeConfig {
  std::string journal_path;
  std::string context_id{"kana-normalization"};
  std::size_t max_journal_events{4096};
  std::size_t max_journal_transactions{2048};
  std::size_t max_journal_payload_bytes{256U * 1024U};
  std::size_t max_journal_identity_bytes{4U * 1024U * 1024U};
  std::size_t max_candidates_per_decode{64};
  bool remove_journal_on_destroy{false};
};

struct SurgeRuntimeMetrics {
  std::size_t claims{0};
  std::size_t evidence{0};
  std::size_t entities{0};
  std::size_t contradictions{0};
  std::size_t questions{0};
  std::size_t chaos_members{0};
  std::uint64_t state_version{0};
};

struct SurgeNormalizationResult {
  bool committed{false};
  std::string output;
  std::string error;
  SurgeRuntimeMetrics metrics;
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
  SurgeNormalizationResult normalize(const std::string& reading,
                                      const std::vector<DecodedPath>& paths,
                                      bool finalize = true);
  bool commit_output(const std::string& output, std::uint64_t source_revision,
                     bool final, std::string& error);

  std::uint64_t state_version() const;
  std::size_t graph_node_count() const;

 private:
  struct CandidateRecord;
  bool ensure_context_cluster(std::string& error);
  SurgeRuntimeConfig config_;
  std::unique_ptr<surge::EventJournal> journal_;
  std::unique_ptr<surge::ContextGraph> graph_;
  std::unique_ptr<surge::ChaosClusterLedger> chaos_;
  mutable std::mutex mutex_;
  std::uint64_t sequence_{1};
  std::string context_cluster_id_;
  std::string chaos_cluster_id_;
  bool opened_{false};
};

}  // namespace kana_decoder
