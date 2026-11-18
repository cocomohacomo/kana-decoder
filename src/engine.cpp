#include "kana_decoder/engine.hpp"
#include "kana_decoder/pack_manifest.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <utility>
namespace {
bool valid_utf8_impl(const std::string& s) {
 for (std::size_t i=0; i<s.size();) {
  unsigned char c=static_cast<unsigned char>(s[i]); std::size_t n=0;
  if (c<=0x7f) n=1; else if(c>=0xc2&&c<=0xdf) n=2; else if(c>=0xe0&&c<=0xef) n=3; else if(c>=0xf0&&c<=0xf4) n=4; else return false;
  if(i+n>s.size()) return false;
  for(std::size_t j=1;j<n;++j) if((static_cast<unsigned char>(s[i+j])&0xc0)!=0x80) return false;
  if(n==3){unsigned char x=static_cast<unsigned char>(s[i+1]);if((c==0xe0&&x<0xa0)||(c==0xed&&x>=0xa0))return false;}
  if(n==4){unsigned char x=static_cast<unsigned char>(s[i+1]);if((c==0xf0&&x<0x90)||(c==0xf4&&x>=0x90))return false;}
  i+=n;
 }
 return true;
}
std::vector<std::string> utf8_chars(const std::string&s){std::vector<std::string>o;if(!valid_utf8_impl(s))return o;for(std::size_t i=0;i<s.size();){unsigned char c=static_cast<unsigned char>(s[i]);std::size_t n=c<0x80?1:(c&0xe0)==0xc0?2:(c&0xf0)==0xe0?3:4;o.push_back(s.substr(i,n));i+=n;}return o;}
std::string join_utf8(const std::vector<std::string>&v){std::string o;for(const auto&c:v)o+=c;return o;}
std::string pair_key(std::int32_t a,std::int32_t b){return std::to_string(a)+":"+std::to_string(b);}
void sat_add(std::uint64_t& target,std::uint64_t value){target=std::numeric_limits<std::uint64_t>::max()-target<value?std::numeric_limits<std::uint64_t>::max():target+value;}
bool parse_i32_strict(const std::string& text,std::int32_t& value,bool nonnegative=false){
 if(text.empty())return false;
 auto result=std::from_chars(text.data(),text.data()+text.size(),value);
 return result.ec==std::errc{}&&result.ptr==text.data()+text.size()&&(!nonnegative||value>=0);
}
bool parse_u32_strict(const std::string& text,std::uint32_t& value){
 if(text.empty())return false;
 auto result=std::from_chars(text.data(),text.data()+text.size(),value);
 return result.ec==std::errc{}&&result.ptr==text.data()+text.size();
}
std::size_t byte_offset_for_codepoints(const std::string&s,std::size_t count){auto chars=utf8_chars(s);if(count>chars.size())return std::string::npos;std::size_t n=0;for(std::size_t i=0;i<count;++i)n+=chars[i].size();return n;}
}
namespace kana_decoder {
bool is_valid_utf8(const std::string& text){return valid_utf8_impl(text);}
std::vector<DecodedPath> decode_reading(const std::string& reading,const LexiconTrie& lexicon,const MaskedNgramScorer& scorer,const DecoderOptions& options,const ConnectionModel* connections,std::vector<CandidateScoreTrace>* trace){
 if(!valid_utf8_impl(reading))return {};auto chars=utf8_chars(reading);if(chars.empty())return {};auto lattice=lexicon.build_lattice(reading,16,true);MorphologyDecoder decoder;return decoder.decode(chars.size(),lattice,scorer,{},options,connections,trace);
}
std::string render_ambiguity(const std::vector<Candidate>&c,bool other){std::ostringstream o;o<<'[';bool f=true;for(const auto&x:c){if(x.surface.empty())continue;if(!f)o<<'/';o<<x.surface;f=false;}if(other){if(!f)o<<'/';o<<"other";}o<<']';return o.str();}
bool DependencyGraph::add(HypothesisNode n){if(n.id.empty()||nodes_.count(n.id))return false;for(const auto&d:n.dependencies){auto i=nodes_.find(d);if(i==nodes_.end()||static_cast<int>(i->second.layer)>static_cast<int>(n.layer))return false;}nodes_.emplace(n.id,std::move(n));return true;}
std::vector<std::string> DependencyGraph::invalidate_dependents(const std::string&id){if(!nodes_.count(id))return{};std::set<std::string>a;bool changed=true;while(changed){changed=false;for(auto&e:nodes_){if(e.first==id||a.count(e.first))continue;for(const auto&d:e.second.dependencies)if(d==id||a.count(d)){a.insert(e.first);changed=true;break;}}}std::vector<std::string>v(a.begin(),a.end());for(const auto&k:v){auto&n=nodes_.at(k);n.valid=false;++n.revision;}return v;}
const HypothesisNode*DependencyGraph::find(const std::string&id)const{auto i=nodes_.find(id);return i==nodes_.end()?nullptr:&i->second;}
}
