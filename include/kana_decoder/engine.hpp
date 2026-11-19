#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
namespace kana_decoder {
enum class Layer { Morphological = 0, Semantic = 1, Orthographic = 2 };
// Provider-neutral grammatical metadata. Values are interpreted only by the
// LanguageAdapter named by language_tag; the host does not assume a POS schema.
struct MorphologyDescriptor {
 std::string language_tag;
 std::string part_of_speech;
 std::string inflection_class;
 std::string inflection_form;
 bool empty() const {
  return language_tag.empty() && part_of_speech.empty() &&
         inflection_class.empty() && inflection_form.empty();
 }
};
struct Candidate {
 std::string id;
 std::string surface;
 MorphologyDescriptor morphology;
};
struct AmbiguityResult { std::string output; bool ambiguous=false; double top_score=0.0; double margin=0.0; std::vector<Candidate> ranked; };
struct HypothesisNode { std::string id; Layer layer; std::vector<std::string> dependencies; std::uint64_t revision=0; bool valid=true; };
class DependencyGraph {
public: bool add(HypothesisNode); std::vector<std::string> invalidate_dependents(const std::string&); const HypothesisNode* find(const std::string&) const;
private: std::unordered_map<std::string,HypothesisNode> nodes_;
};

bool is_valid_utf8(const std::string& text);
struct DecodedToken {
 std::size_t start=0, end=0;
 Candidate candidate;
 double morphology_score=0.0, context_score=0.0;
 double lexical_score=0.0, transition_score=0.0, ngram_score=0.0;
 double cluster_score=0.0, cluster_raw_evidence=0.0, pos_score=0.0;
 bool has_transition_evidence=false, has_ngram_evidence=false;
 bool has_cluster_evidence=false, has_pos_evidence=false;
};
struct DecodedPath {
 std::vector<DecodedToken> tokens;
 double score=0.0;
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
 // Provider-neutral state acceptance: the LanguagePack supplies decoded
 // paths, while this host session owns only bounded input/revision state.
 bool append_decoded(const std::string& chunk,std::vector<DecodedPath> paths,
   StreamingSnapshot& snapshot,std::size_t max_input_codepoints=256);
 bool commit_common_prefix(StreamingSnapshot& snapshot,std::string& committed_output);
 bool commit_best(StreamingSnapshot& snapshot,std::string& committed_output);
 void reset();
 const std::string& reading() const { return reading_; }
private:
 std::size_t max_reading_codepoints_;
 std::uint64_t revision_=0;
 std::string reading_;
};
std::string render_ambiguity(const std::vector<Candidate>&,bool);
}
