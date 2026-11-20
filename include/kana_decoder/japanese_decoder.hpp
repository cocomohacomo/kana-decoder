#pragma once

#include "kana_decoder/engine.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace kana_decoder {

enum class OutputMode { Normal, Ambiguous, Conservative, Diagnostic };
enum class CorrectionKind { Insert, Delete, Replace, Duplicate, Phonetic };

class MaskedNgramScorer {
public:
 void add_left_bigram(const std::string&,const std::string&,std::uint32_t);
 void add_right_bigram(const std::string&,const std::string&,std::uint32_t);
 void add_left_ngram(const std::vector<std::string>&,const std::string&,std::uint32_t);
 void add_left_skipgram(const std::string&,const std::string&,std::uint32_t);
 void add_right_ngram(const std::string&,const std::vector<std::string>&,std::uint32_t);
 void add_unigram(const std::string&,std::uint32_t);
 bool set_smoothing_alpha(double);
 double smoothing_alpha() const { return smoothing_alpha_; }
 bool load_tsv(const std::string& path);
 double score(const std::string&,const std::vector<std::string>&,const std::vector<std::string>&) const;
 double contextual_score(const std::string&,const std::vector<std::string>&,const std::vector<std::string>&) const;
 bool has_left_context_evidence(const std::string&,const std::vector<std::string>&) const;
 bool has_right_context_evidence(const std::string&,const std::vector<std::string>&) const;
 double skip_contextual_score(const std::string&,const std::vector<std::string>&) const;
 AmbiguityResult resolve(const std::vector<Candidate>&,const std::vector<std::string>&,const std::vector<std::string>&,
  OutputMode,double minimum_score=-8.0,double minimum_margin=1.25,bool include_other=true,const std::string& original_reading={}) const;
private:
 std::unordered_map<std::string,std::uint64_t> left_counts_,right_counts_,unigram_counts_;
 std::unordered_map<std::string,std::uint64_t> left_context_totals_,right_context_totals_;
 std::unordered_map<std::string,std::uint64_t> skip_counts_,skip_context_totals_;
 std::uint64_t unigram_total_=0;
 double smoothing_alpha_=0.5;
 static std::string key(const std::string&,const std::string&);
 static std::string context_key(const std::vector<std::string>&);
};
struct EditOperation { CorrectionKind kind; std::size_t position; std::string before,after; };
struct CorrectionCandidate { std::string original,proposed; EditOperation edit; double score=0.0; bool auto_accepted=false; std::string rule_id; };
struct CorrectionRule { std::string from,to,rule_id; CorrectionKind kind=CorrectionKind::Phonetic; double cost=1.0; };
struct CorrectionOptions { bool enabled=true,protected_span=false; std::size_t max_candidates=8; double minimum_improvement=2.0,minimum_score=1.0; std::vector<CorrectionRule> rules; };
class CorrectionEngine { public: std::vector<CorrectionCandidate> generate(const std::string&,const std::unordered_map<std::string,double>&,const CorrectionOptions& = {}) const; };

struct LatticeCandidate {
 std::string id, surface;
 std::int32_t word_cost=0, connection_cost=0;
 std::int32_t left_id=0, right_id=0;
 std::string pos,inflection_type,inflection_form;
};
class ConnectionModel {
public:
 void set_cost(std::int32_t right_id,std::int32_t left_id,std::int32_t cost);
 std::int32_t cost(std::int32_t right_id,std::int32_t left_id) const;
 bool load_tsv(const std::string& path);
 std::size_t nonzero_cells() const { return costs_.size(); }
 std::int32_t left_size() const { return left_size_; }
 std::int32_t right_size() const { return right_size_; }
private:
 struct Entry { std::uint64_t key=0; std::int32_t value=0; };
 std::vector<Entry> costs_;
 std::int32_t left_size_=0,right_size_=0;
 static std::uint64_t packed_key(std::int32_t right_id,std::int32_t left_id);
};

struct LatticeEdge {
 std::size_t start=0, end=0; // Unicode code-point offsets in the reading
 std::vector<LatticeCandidate> candidates;
};

struct CorrectionDecodeResult {
 std::string input_reading, decoded_reading;
 bool correction_applied=false;
 CorrectionCandidate correction{"", "", {CorrectionKind::Phonetic, 0, "", ""}, 0.0, false, ""};
 std::vector<DecodedPath> paths;
};

struct CandidateScoreTrace {
 std::size_t start=0,end=0;
 std::string candidate_id,surface;
 std::int32_t word_cost=0,candidate_connection_cost=0,transition_cost=0;
 double raw_context_score=0.0,context_evidence_score=0.0,context_weight=1.0,morphology_score=0.0,weighted_context_score=0.0,local_score=0.0;
 bool unknown_fallback=false;
};
struct LatticeMatchTrace {
 std::size_t start=0,end=0;
 std::size_t available_candidate_count=0,retained_candidate_count=0,unique_surface_count=0;
 std::vector<LatticeCandidate> candidates;
};
struct LatticePositionTrace {
 std::size_t start=0;
 std::size_t dictionary_match_count=0;
 std::size_t available_dictionary_match_count=0,pruned_dictionary_match_count=0;
 std::size_t available_candidate_count=0,retained_candidate_count=0,pruned_candidate_count=0;
 bool fallback_enabled=false;
 bool fallback_generated=false;
 bool fallback_suppressed_by_dictionary=false;
 std::vector<LatticeMatchTrace> dictionary_matches;
};
class SurgeClusterModel {
public:
 bool load_tsv(const std::string& path);
 double evidence_score(const std::string& candidate,
                       const std::vector<std::string>& history,
                       bool* has_evidence=nullptr) const;
 double gap_threshold() const { return gap_threshold_; }
 std::size_t membership_count() const { return token_cluster_.size(); }
 std::size_t evidence_count() const { return evidence_.size(); }
private:
 std::unordered_map<std::string,std::uint32_t> token_cluster_;
 std::map<std::string,std::uint32_t> evidence_;
 std::map<std::string,std::uint32_t> totals_;
 double gap_threshold_=0.0;
 double smoothing_alpha_=0.5;
 std::size_t vocab_size_=1;
 static std::string evidence_key(std::uint32_t,std::uint32_t,const std::string&);
 static std::string total_key(std::uint32_t,std::uint32_t);
};
struct DecoderOptions {
 std::size_t beam_width=8;
 std::size_t max_edges_per_position=16;
 std::size_t max_candidates_per_edge=8;
 std::size_t max_paths=8;
 double context_weight=1.0;
 const MaskedNgramScorer* pos_ngram_scorer=nullptr;
 double pos_context_weight=0.25;
 bool surge_left_context_gate=false;
 const SurgeClusterModel* surge_cluster_model=nullptr;
 double surge_cluster_weight=1.0;
 double surge_context_margin=0.5;
 // Deterministic per-decode resource ceilings. Exceeding a ceiling rejects
 // the decode rather than returning a silently truncated/low-quality result.
 std::size_t max_input_codepoints=256;
 std::size_t max_lattice_edges=4096;
 std::size_t max_lattice_candidates=32768;
 std::size_t max_expanded_states=262144;
 std::size_t max_trace_records=4096;
};

struct TrieStorageStats {
 std::size_t frozen_node_capacity_bytes=0;
 std::size_t edge_arena_capacity_bytes=0;
 std::size_t candidate_arena_capacity_bytes=0;
 std::size_t builder_node_capacity_bytes=0;
 std::size_t builder_edge_capacity_bytes=0;
 std::size_t builder_candidate_capacity_bytes=0;
 std::size_t string_pool_capacity_bytes=0;
 std::size_t estimated_bytes=0;
};

class LexiconTrie {
public:
 bool add(const std::string& reading,const LatticeCandidate& candidate);
 bool load_tsv(const std::string& path,std::size_t* loaded=nullptr);
 std::vector<LatticeEdge> build_lattice(const std::string& reading,
   std::size_t max_matches_per_start=16,bool kana_fallback=true,
   std::vector<LatticePositionTrace>* trace=nullptr, 
   std::size_t max_total_edges=4096,std::size_t max_total_candidates=32768,
   bool* resource_limit_exceeded=nullptr) const;
 std::size_t size() const { return entry_count_; }
 void build_candidate_pos_index();
 std::string candidate_pos(const std::string& candidate_id) const;
 // Read-only diagnostics for validating compact dictionary storage.
 std::size_t candidate_string_pool_bytes() const { return candidate_string_pool_.size(); }
 std::size_t interned_metadata_string_count() const { return interned_metadata_strings_.size(); }
 std::size_t candidate_pos_index_size() const { return candidate_pos_index_.size(); }
 TrieStorageStats storage_stats() const {
  TrieStorageStats stats;
  stats.frozen_node_capacity_bytes=nodes_.capacity()*sizeof(Node);
  stats.edge_arena_capacity_bytes=edge_arena_.capacity()*sizeof(edge_arena_[0]);
  stats.candidate_arena_capacity_bytes=candidate_arena_.capacity()*sizeof(CompactCandidate);
  stats.builder_node_capacity_bytes=build_nodes_.capacity()*sizeof(BuildNode);
  for(const auto& node:build_nodes_){
   stats.builder_edge_capacity_bytes+=node.next.capacity()*sizeof(node.next[0]);
   stats.builder_candidate_capacity_bytes+=node.values.capacity()*sizeof(CompactCandidate);
  }
  stats.string_pool_capacity_bytes=candidate_string_pool_.capacity();
  stats.estimated_bytes=stats.frozen_node_capacity_bytes+stats.edge_arena_capacity_bytes+
   stats.candidate_arena_capacity_bytes+stats.builder_node_capacity_bytes+
   stats.builder_edge_capacity_bytes+stats.builder_candidate_capacity_bytes+
   stats.string_pool_capacity_bytes;
  return stats;
 }
private:
 struct CompactString { std::uint32_t offset=0, size=0; };
 struct CompactCandidate {
  CompactString id, surface, pos, inflection_type, inflection_form;
  std::int32_t word_cost=0, connection_cost=0, left_id=0, right_id=0;
 };
 struct PosIndexEntry { CompactString id, pos; };
 struct Node { std::uint32_t edge_offset=0, edge_count=0, candidate_offset=0, candidate_count=0; };
 struct BuildNode { std::vector<std::pair<std::uint32_t,std::uint32_t>> next; std::vector<CompactCandidate> values; };
 std::vector<PosIndexEntry> candidate_pos_index_;
 // POS and inflection labels have a small repeated vocabulary. Keep one pool
 // copy per distinct metadata value instead of one copy per candidate.
 std::unordered_map<std::string,CompactString> interned_metadata_strings_;
 std::string candidate_string_pool_;
 std::vector<Node> nodes_{1};
 std::vector<BuildNode> build_nodes_{1};
 CompactString store_candidate_string(const std::string& value);
 CompactString store_metadata_string(const std::string& value);
 std::string_view candidate_string(CompactString value) const;
 LatticeCandidate materialize_candidate(const CompactCandidate& value) const;
 std::vector<std::pair<std::uint32_t,std::uint32_t>> edge_arena_;
 std::vector<CompactCandidate> candidate_arena_;
 bool edges_compacted_=false, candidates_compacted_=false;
 void compact_edges();
 void expand_edges();
 void compact_candidates();
 void expand_candidates();
 std::size_t entry_count_=0;
};

bool load_language_pack(const std::string& directory,LexiconTrie& lexicon,MaskedNgramScorer& scorer,std::size_t* loaded_entries=nullptr,ConnectionModel* connections=nullptr,CorrectionOptions* corrections=nullptr,std::string* error=nullptr);
std::vector<DecodedPath> decode_reading(const std::string& reading,
 const LexiconTrie& lexicon,const MaskedNgramScorer& scorer,
 const DecoderOptions& options={},const ConnectionModel* connections=nullptr,
 std::vector<CandidateScoreTrace>* trace=nullptr,
 std::vector<LatticePositionTrace>* lattice_trace=nullptr, 
 bool* resource_limit_exceeded=nullptr);
CorrectionDecodeResult decode_with_corrections(const std::string& reading,
 const LexiconTrie& lexicon,const MaskedNgramScorer& scorer,
 const CorrectionOptions& correction_options={},const DecoderOptions& decoder_options={},
 const ConnectionModel* connections=nullptr);
class MorphologyDecoder {
public:
 std::vector<DecodedPath> decode(std::size_t reading_length,
  const std::vector<LatticeEdge>& lattice,const MaskedNgramScorer& scorer,
  const std::vector<std::string>& right_context={},
  const DecoderOptions& options={},const ConnectionModel* connections=nullptr,
  std::vector<CandidateScoreTrace>* trace=nullptr,
  bool* resource_limit_exceeded=nullptr) const;
};

}  // namespace kana_decoder
