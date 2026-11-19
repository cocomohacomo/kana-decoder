#include "kana_decoder/japanese_decoder.hpp"
#include <algorithm>
#include <string>
#include <vector>
namespace kana_decoder {
namespace {
std::vector<std::string> utf8_chars(const std::string& s) {
 std::vector<std::string> out;
 if (!is_valid_utf8(s)) return out;
 for (std::size_t i=0; i<s.size();) {
  const unsigned char c=static_cast<unsigned char>(s[i]);
  const std::size_t n=c<0x80?1:(c&0xe0)==0xc0?2:(c&0xf0)==0xe0?3:4;
  out.push_back(s.substr(i,n)); i+=n;
 }
 return out;
}
}
std::vector<DecodedPath> decode_reading(const std::string& reading,const LexiconTrie& lexicon,const MaskedNgramScorer& scorer,const DecoderOptions& options,const ConnectionModel* connections,std::vector<CandidateScoreTrace>* trace,std::vector<LatticePositionTrace>* lattice_trace,bool* resource_limit_exceeded){
 if(resource_limit_exceeded)*resource_limit_exceeded=false;
 if (!is_valid_utf8(reading)) return {};
 const std::size_t input_limit=std::min<std::size_t>(options.max_input_codepoints,256);
 const std::size_t edge_limit=std::min<std::size_t>(options.max_lattice_edges,4096);
 const std::size_t candidate_limit=std::min<std::size_t>(options.max_lattice_candidates,32768);
 const std::size_t expansion_limit=std::min<std::size_t>(options.max_expanded_states,262144);
 if(input_limit==0||reading.size()>input_limit*4||
    edge_limit==0||candidate_limit==0||expansion_limit==0){
  if(resource_limit_exceeded)*resource_limit_exceeded=true;
  return {};
 }
 auto chars = utf8_chars(reading);
 if (chars.empty()) return {};
 if(chars.size()>input_limit){
  if(resource_limit_exceeded)*resource_limit_exceeded=true;
  return {};
 }
 bool lattice_limit=false;
 auto lattice = lexicon.build_lattice(reading, options.max_edges_per_position, true,
  lattice_trace,edge_limit,candidate_limit,&lattice_limit);
 if(lattice_limit){
  if(resource_limit_exceeded)*resource_limit_exceeded=true;
  return {};
 }
 MorphologyDecoder decoder;
 DecoderOptions bounded_options=options;
 bounded_options.max_expanded_states=expansion_limit;
 return decoder.decode(chars.size(), lattice, scorer, {}, bounded_options, connections, trace,
  resource_limit_exceeded);
}

}  // namespace kana_decoder
