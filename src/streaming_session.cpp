#include "kana_decoder/engine.hpp"
#include <algorithm>
namespace {
std::vector<std::string> utf8_chars(const std::string&s){std::vector<std::string>o;if(!kana_decoder::is_valid_utf8(s))return o;for(std::size_t i=0;i<s.size();){unsigned char c=static_cast<unsigned char>(s[i]);std::size_t n=c<0x80?1:(c&0xe0)==0xc0?2:(c&0xf0)==0xe0?3:4;o.push_back(s.substr(i,n));i+=n;}return o;}
std::size_t byte_offset_for_codepoints(const std::string&s,std::size_t count){auto chars=utf8_chars(s);if(count>chars.size())return std::string::npos;std::size_t n=0;for(std::size_t i=0;i<count;++i)n+=chars[i].size();return n;}
}
namespace kana_decoder {
StreamingSession::StreamingSession(std::size_t limit):max_reading_codepoints_(limit?limit:1){}
bool StreamingSession::append(const std::string& chunk,const LexiconTrie& lexicon,const MaskedNgramScorer& scorer,StreamingSnapshot& snapshot,const DecoderOptions& options,const ConnectionModel* connections){
 if (chunk.empty() || !is_valid_utf8(chunk)) return false;
 auto current = utf8_chars(reading_);
 auto incoming = utf8_chars(chunk);
 if (current.size() + incoming.size() > max_reading_codepoints_) return false;
 std::string candidate=reading_+chunk;auto paths=decode_reading(candidate,lexicon,scorer,options,connections);if(paths.empty())return false;
 reading_=std::move(candidate);++revision_;snapshot.revision=revision_;snapshot.reading=reading_;snapshot.paths=std::move(paths);snapshot.provisional=true;return true;
}

bool StreamingSession::commit_common_prefix(StreamingSnapshot& snapshot,std::string& output){
 if(reading_.empty()||snapshot.revision!=revision_||snapshot.reading!=reading_||snapshot.paths.empty())return false;
 std::size_t common=snapshot.paths.front().tokens.size();
 for(const auto& path:snapshot.paths)common=std::min(common,path.tokens.size());
 std::size_t matched=0;
 for(;matched<common;++matched){
  const auto& base=snapshot.paths.front().tokens[matched];bool same=true;
  for(std::size_t p=1;p<snapshot.paths.size();++p){const auto& token=snapshot.paths[p].tokens[matched];
   if(token.start!=base.start||token.end!=base.end||token.candidate.id!=base.candidate.id||token.candidate.surface!=base.candidate.surface){same=false;break;}
  }
  if(!same)break;
 }
 if(matched==0)return false;
 const std::size_t cut_cp=snapshot.paths.front().tokens[matched-1].end;
 const std::size_t cut_bytes=byte_offset_for_codepoints(reading_,cut_cp);
 if(cut_bytes==std::string::npos||cut_bytes==0)return false;
 std::string committed;for(std::size_t i=0;i<matched;++i)committed+=snapshot.paths.front().tokens[i].candidate.surface;
 if(committed.empty())return false;
 for(auto& path:snapshot.paths){path.tokens.erase(path.tokens.begin(),path.tokens.begin()+static_cast<std::ptrdiff_t>(matched));
  for(auto& token:path.tokens){token.start-=cut_cp;token.end-=cut_cp;}
 }
 reading_.erase(0,cut_bytes);++revision_;snapshot.revision=revision_;snapshot.reading=reading_;snapshot.provisional=!reading_.empty();output=std::move(committed);
 if(reading_.empty())snapshot.paths.clear();
 return true;
}
bool StreamingSession::commit_best(StreamingSnapshot& snapshot,std::string& output){
 if(reading_.empty()||snapshot.revision!=revision_||snapshot.paths.empty()||snapshot.reading!=reading_)return false;
 std::string committed;for(const auto&token:snapshot.paths.front().tokens)committed+=token.candidate.surface;
 if(committed.empty())return false;
 output=std::move(committed);snapshot.provisional=false;++revision_;snapshot.revision=revision_;
 reading_.clear();return true;
}
void StreamingSession::reset(){reading_.clear();++revision_;}
}
