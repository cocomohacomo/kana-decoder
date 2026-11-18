#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
namespace kana_decoder {
enum class OutputMode { Normal, Ambiguous, Conservative, Diagnostic };
enum class Layer { Morphological = 0, Semantic = 1, Orthographic = 2 };
enum class CorrectionKind { Insert, Delete, Replace, Duplicate, Phonetic };
struct Candidate { std::string id; std::string surface; std::string pos,inflection_type,inflection_form; };
struct AmbiguityResult { std::string output; bool ambiguous=false; double top_score=0.0; double margin=0.0; std::vector<Candidate> ranked; };
class MaskedNgramScorer {
public:
 void add_left_bigram(const std::string&,const std::string&,std::uint32_t);
 void add_right_bigram(const std::string&,const std::string&,std::uint32_t);
 void add_left_ngram(const std::vector<std::string>&,const std::string&,std::uint32_t);
 void add_right_ngram(const std::string&,const std::vector<std::string>&,std::uint32_t);
 void add_unigram(const std::string&,std::uint32_t);
 bool load_tsv(const std::string& path);
 double score(const std::string&,const std::vector<std::string>&,const std::vector<std::string>&) const;
 AmbiguityResult resolve(const std::vector<Candidate>&,const std::vector<std::string>&,const std::vector<std::string>&,
  OutputMode,double minimum_score=-8.0,double minimum_margin=1.25,bool include_other=true,const std::string& original_reading={}) const;
private:
 std::unordered_map<std::string,std::uint64_t> left_counts_,right_counts_,unigram_counts_;
 std::unordered_map<std::string,std::uint64_t> left_context_totals_,right_context_totals_;
 std::uint64_t unigram_total_=0;
 static std::string key(const std::string&,const std::string&);
 static std::string context_key(const std::vector<std::string>&);
};
struct EditOperation { CorrectionKind kind; std::size_t position; std::string before,after; };
struct CorrectionCandidate { std::string original,proposed; EditOperation edit; double score=0.0; bool auto_accepted=false; std::string rule_id; };
struct CorrectionRule { std::string from,to,rule_id; CorrectionKind kind=CorrectionKind::Phonetic; double cost=1.0; };
struct CorrectionOptions { bool enabled=true,protected_span=false; std::size_t max_candidates=8; double minimum_improvement=2.0,minimum_score=1.0; std::vector<CorrectionRule> rules; };
class CorrectionEngine { public: std::vector<CorrectionCandidate> generate(const std::string&,const std::unordered_map<std::string,double>&,const CorrectionOptions& = {}) const; };
struct HypothesisNode { std::string id; Layer layer; std::vector<std::string> dependencies; std::uint64_t revision=0; bool valid=true; };
class DependencyGraph {
public: bool add(HypothesisNode); std::vector<std::string> invalidate_dependents(const std::string&); const HypothesisNode* find(const std::string&) const;
private: std::unordered_map<std::string,HypothesisNode> nodes_;
};

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
bool is_valid_utf8(const std::string& text);
struct LatticeEdge {
 std::size_t start=0, end=0; // Unicode code-point offsets in the reading
 std::vector<LatticeCandidate> candidates;
};
struct DecodedToken {
 std::size_t start=0, end=0;
 Candidate candidate;
 double morphology_score=0.0, context_score=0.0;
};
struct DecodedPath {
 std::vector<DecodedToken> tokens;
 double score=0.0;
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
 double raw_context_score=0.0,context_weight=1.0,morphology_score=0.0,weighted_context_score=0.0,local_score=0.0;
 bool unknown_fallback=false;
};
struct LatticeMatchTrace {
 std::size_t start=0,end=0;
 std::vector<LatticeCandidate> candidates;
};
struct LatticePositionTrace {
 std::size_t start=0;
 std::size_t dictionary_match_count=0;
 bool fallback_enabled=false;
 bool fallback_generated=false;
 bool fallback_suppressed_by_dictionary=false;
 std::vector<LatticeMatchTrace> dictionary_matches;
};
struct DecoderOptions {
 std::size_t beam_width=8;
 std::size_t max_edges_per_position=16;
 std::size_t max_candidates_per_edge=8;
 std::size_t max_paths=8;
 double context_weight=1.0;
 // Deterministic per-decode resource ceilings. Exceeding a ceiling rejects
 // the decode rather than returning a silently truncated/low-quality result.
 std::size_t max_input_codepoints=256;
 std::size_t max_lattice_edges=4096;
 std::size_t max_lattice_candidates=32768;
 std::size_t max_expanded_states=32768;
 std::size_t max_trace_records=4096;
};

class LexiconTrie {
public:
 bool add(const std::string& reading,const LatticeCandidate& candidate);
 bool load_tsv(const std::string& path,std::size_t* loaded=nullptr);
 std::vector<LatticeEdge> build_lattice(const std::string& reading,
   std::size_t max_matches_per_start=16,bool kana_fallback=true,
   std::vector<LatticePositionTrace>* trace=nullptr,\n   std::size_t max_total_edges=4096,std::size_t max_total_candidates=32768,\n   bool* resource_limit_exceeded=nullptr) const;
 std::size_t size() const { return entry_count_; }
private:
 struct Node { std::unordered_map<std::string,std::size_t> next; std::vector<LatticeCandidate> values; };
 std::vector<Node> nodes_{1};
 std::size_t entry_count_=0;
};
struct StreamingSnapshot {
 std::uint64_t revision=0;
 std::string reading;
 std::vector<DecodedPath> paths;
 bool provisional=true;
};
class StreamingSession {
public:
 explicit StreamingSession(std::size_t max_reading_codepoints=256);
 bool append(const std::string& chunk,const LexiconTrie& lexicon,
   const MaskedNgramScorer& scorer,StreamingSnapshot& snapshot,
   const DecoderOptions& options={},const ConnectionModel* connections=nullptr);
 bool commit_common_prefix(StreamingSnapshot& snapshot,std::string& committed_output);
 bool commit_best(StreamingSnapshot& snapshot,std::string& committed_output);
 void reset();
 const std::string& reading() const { return reading_; }
private:
 std::size_t max_reading_codepoints_;
 std::uint64_t revision_=0;
 std::string reading_;
};
bool load_language_pack(const std::string& directory,LexiconTrie& lexicon,MaskedNgramScorer& scorer,std::size_t* loaded_entries=nullptr,ConnectionModel* connections=nullptr,CorrectionOptions* corrections=nullptr);
std::vector<DecodedPath> decode_reading(const std::string& reading,
 const LexiconTrie& lexicon,const MaskedNgramScorer& scorer,
 const DecoderOptions& options={},const ConnectionModel* connections=nullptr,
 std::vector<CandidateScoreTrace>* trace=nullptr,
 std::vector<LatticePositionTrace>* lattice_trace=nullptr,\n bool* resource_limit_exceeded=nullptr);
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
  std::vector<CandidateScoreTrace>* trace=nullptr) const;
};
std::string render_ambiguity(const std::vector<Candidate>&,bool);
}
