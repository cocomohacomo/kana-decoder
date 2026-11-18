#include "kana_decoder/engine.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>
#include <unordered_map>
namespace {
std::vector<std::string> utf8_chars(const std::string&s){std::vector<std::string>o;if(!kana_decoder::is_valid_utf8(s))return o;for(std::size_t i=0;i<s.size();){unsigned char c=static_cast<unsigned char>(s[i]);std::size_t n=c<0x80?1:(c&0xe0)==0xc0?2:(c&0xf0)==0xe0?3:4;o.push_back(s.substr(i,n));i+=n;}return o;}
std::string join_utf8(const std::vector<std::string>&v){std::string o;for(const auto&c:v)o+=c;return o;}
}
namespace kana_decoder {
CorrectionDecodeResult decode_with_corrections(const std::string& reading,const LexiconTrie& lexicon,
 const MaskedNgramScorer& scorer,const CorrectionOptions& correction_options,
 const DecoderOptions& decoder_options,const ConnectionModel* connections){
 CorrectionDecodeResult result;result.input_reading=reading;result.decoded_reading=reading;
 result.paths=decode_reading(reading,lexicon,scorer,decoder_options,connections);
 if(result.paths.empty()||!correction_options.enabled||correction_options.protected_span||
    correction_options.max_candidates==0)return result;
 const bool protected_ascii=std::any_of(reading.begin(),reading.end(),[](unsigned char c){return c>='0'&&c<='9';});
 const bool protected_url=reading.find("://")!=std::string::npos||reading.find("www.")==0||reading.find('@')!=std::string::npos;
 if(protected_ascii||protected_url)return result;
 std::unordered_map<std::string,double> scores;
 std::unordered_map<std::string,std::vector<DecodedPath>> decoded;
 scores[reading]=result.paths.front().score;
 const auto chars=utf8_chars(reading);
 std::vector<std::string> proposals;
 auto add_proposal=[&](const std::string& candidate){
  if(candidate.empty()||candidate==reading||proposals.size()>=std::min<std::size_t>(correction_options.max_candidates,8))return;
  if(!is_valid_utf8(candidate)||std::find(proposals.begin(),proposals.end(),candidate)!=proposals.end())return;
  proposals.push_back(candidate);
 };
 for(std::size_t i=1;i<chars.size()&&proposals.size()<8;++i){
  if(chars[i]==chars[i-1]){auto copy=chars;copy.erase(copy.begin()+static_cast<std::ptrdiff_t>(i));add_proposal(join_utf8(copy));}
 }
 for(const auto& rule:correction_options.rules){
  if(proposals.size()>=std::min<std::size_t>(correction_options.max_candidates,8))break;
  if(rule.from.empty()){
   if(rule.to.empty()||!is_valid_utf8(rule.to))continue;
   std::size_t byte=0;
   for(std::size_t i=0;i<=chars.size()&&proposals.size()<std::min<std::size_t>(correction_options.max_candidates,8);++i){
    std::string candidate=reading;candidate.insert(byte,rule.to);add_proposal(candidate);
    if(i<chars.size())byte+=chars[i].size();
   }
  }else{
   if(!is_valid_utf8(rule.from)||!is_valid_utf8(rule.to))continue;
   std::size_t pos=reading.find(rule.from);
   while(pos!=std::string::npos&&proposals.size()<std::min<std::size_t>(correction_options.max_candidates,8)){
    std::string candidate=reading;candidate.replace(pos,rule.from.size(),rule.to);add_proposal(candidate);
    pos=reading.find(rule.from,pos+rule.from.size());
   }
  }
 }
 for(const auto& proposal:proposals){
  auto paths=decode_reading(proposal,lexicon,scorer,decoder_options,connections);
  if(!paths.empty()){scores[proposal]=paths.front().score;decoded.emplace(proposal,std::move(paths));}
 }
 CorrectionEngine engine;
 auto candidates=engine.generate(reading,scores,correction_options);
 const CorrectionCandidate* best=nullptr;
 for(const auto& candidate:candidates){
  if(candidate.auto_accepted&&(!best||candidate.score>best->score))best=&candidate;
 }
 if(best){
  auto found=decoded.find(best->proposed);
  if(found!=decoded.end()){
   result.decoded_reading=best->proposed;result.correction=*best;
   result.correction_applied=true;result.paths=std::move(found->second);
  }
 }
 return result;
}

std::vector<CorrectionCandidate> CorrectionEngine::generate(const std::string&input,const std::unordered_map<std::string,double>&scores,const CorrectionOptions&o)const{
 std::vector<CorrectionCandidate>out;bool digit=std::any_of(input.begin(),input.end(),[](unsigned char c){return c>='0'&&c<='9';});bool url=input.find("://")!=std::string::npos||input.find("www.")==0||input.find('@')!=std::string::npos;
 if(!o.enabled||o.protected_span||digit||url||input.empty()||!o.max_candidates)return out;auto oi=scores.find(input);double original=oi==scores.end()?0:oi->second;
 auto add=[&](const std::string&p,CorrectionKind k,std::size_t pos,const std::string&b,const std::string&a,const std::string&id,double cost){if(p==input||out.size()>=o.max_candidates)return;auto it=scores.find(p);if(it==scores.end())return;double adjusted=it->second-cost;bool accept=adjusted-original>=o.minimum_improvement&&adjusted>=o.minimum_score;CorrectionCandidate c{input,p,{k,pos,b,a},adjusted,accept,id};auto d=std::find_if(out.begin(),out.end(),[&](const CorrectionCandidate&v){return v.proposed==p;});if(d==out.end())out.push_back(std::move(c));};
 auto chars=utf8_chars(input);std::size_t duplicate_byte_pos=chars.empty()?0:chars[0].size();for(std::size_t i=1;i<chars.size()&&out.size()<o.max_candidates;++i){if(chars[i]==chars[i-1]){auto e=chars;e.erase(e.begin()+static_cast<std::ptrdiff_t>(i));add(join_utf8(e),CorrectionKind::Duplicate,duplicate_byte_pos,chars[i],"","builtin-duplicate-delete",1.0);}duplicate_byte_pos+=chars[i].size();}
 for(const auto&rule:o.rules){if(out.size()>=o.max_candidates)break;if(rule.from.empty()){if(rule.to.empty())continue;std::size_t pos=0;for(std::size_t i=0;i<=chars.size()&&out.size()<o.max_candidates;++i){std::string p=input;p.insert(pos,rule.to);add(p,rule.kind,pos,"",rule.to,rule.rule_id,rule.cost);if(i<chars.size())pos+=chars[i].size();}continue;}
 std::size_t pos=input.find(rule.from);while(pos!=std::string::npos&&out.size()<o.max_candidates){std::string p=input;p.replace(pos,rule.from.size(),rule.to);add(p,rule.kind,pos,rule.from,rule.to,rule.rule_id,rule.cost);pos=input.find(rule.from,pos+rule.from.size());}}
 return out;
}
}
