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
std::string MaskedNgramScorer::key(const std::string&a,const std::string&b){return a+"\x1f"+b;}
std::string MaskedNgramScorer::context_key(const std::vector<std::string>&c){std::string o;std::size_t b=c.size()>2?c.size()-2:0;for(std::size_t i=b;i<c.size();++i){if(!o.empty())o+="\x1e";o+=c[i];}return o;}
void MaskedNgramScorer::add_left_bigram(const std::string&l,const std::string&c,std::uint32_t n){sat_add(left_counts_[key(l,c)],n);sat_add(left_context_totals_[l],n);}
void MaskedNgramScorer::add_right_bigram(const std::string&c,const std::string&r,std::uint32_t n){sat_add(right_counts_[key(c,r)],n);sat_add(right_context_totals_[r],n);}
void MaskedNgramScorer::add_left_ngram(const std::vector<std::string>&l,const std::string&c,std::uint32_t n){auto ctx=context_key(l);sat_add(left_counts_[key(ctx,c)],n);sat_add(left_context_totals_[ctx],n);}
void MaskedNgramScorer::add_right_ngram(const std::string&c,const std::vector<std::string>&r,std::uint32_t n){auto ctx=context_key(r);sat_add(right_counts_[key(c,ctx)],n);sat_add(right_context_totals_[ctx],n);}
void MaskedNgramScorer::add_unigram(const std::string&c,std::uint32_t n){sat_add(unigram_counts_[c],n);sat_add(unigram_total_,n);}
std::uint64_t ConnectionModel::packed_key(std::int32_t right_id,std::int32_t left_id){
 return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(right_id))<<32)|
        static_cast<std::uint32_t>(left_id);
}
void ConnectionModel::set_cost(std::int32_t right_id,std::int32_t left_id,std::int32_t value){
 if(right_id<0||left_id<0)return;
 const auto key=packed_key(right_id,left_id);
 auto it=std::lower_bound(costs_.begin(),costs_.end(),key,
  [](const Entry& entry,std::uint64_t target){return entry.key<target;});
 if(it!=costs_.end()&&it->key==key){
  if(value==0)costs_.erase(it);else it->value=value;
  return;
 }
 if(value!=0)costs_.insert(it,Entry{key,value});
}
std::int32_t ConnectionModel::cost(std::int32_t right_id,std::int32_t left_id)const{
 if(right_id<0||left_id<0)return 0;
 const auto key=packed_key(right_id,left_id);
 auto it=std::lower_bound(costs_.begin(),costs_.end(),key,
  [](const Entry& entry,std::uint64_t target){return entry.key<target;});
 return it!=costs_.end()&&it->key==key?it->value:0;
}
bool ConnectionModel::load_tsv(const std::string& path){
 std::ifstream in(path);if(!in)return false;ConnectionModel parsed;std::string line;
 while(std::getline(in,line)){
  if(line.empty())continue;
  if(line[0]=='#'){
   std::stringstream meta(line);std::string marker,kind,extra;std::int32_t left=0,right=0;
   if(meta>>marker>>kind&&marker=="#"&&kind=="matrix"){
    if(!(meta>>left>>right)||meta>>extra||left<=0||right<=0)return false;
    if(parsed.left_size_||parsed.right_size_)return false;
    parsed.left_size_=left;parsed.right_size_=right;
   }
   continue;
  }
  std::stringstream ss(line);std::string a,b,v,extra;
  if(!std::getline(ss,a,'\t')||!std::getline(ss,b,'\t')||!std::getline(ss,v,'\t')||std::getline(ss,extra,'\t'))return false;
  std::int32_t right=0,left=0,value=0;
  if(!parse_i32_strict(a,right,true)||!parse_i32_strict(b,left,true)||!parse_i32_strict(v,value))return false;
  if(parsed.left_size_&&(left>=parsed.left_size_||right>=parsed.right_size_))return false;
  parsed.costs_.push_back(Entry{packed_key(right,left),value});
 }
 if(!in.eof())return false;
 std::sort(parsed.costs_.begin(),parsed.costs_.end(),[](const Entry&a,const Entry&b){return a.key<b.key;});
 for(std::size_t i=1;i<parsed.costs_.size();++i)if(parsed.costs_[i-1].key==parsed.costs_[i].key)return false;
 parsed.costs_.erase(std::remove_if(parsed.costs_.begin(),parsed.costs_.end(),[](const Entry& entry){return entry.value==0;}),parsed.costs_.end());
 *this=std::move(parsed);return true;
}


bool MaskedNgramScorer::load_tsv(const std::string& path){
 std::ifstream in(path);if(!in)return false;
 MaskedNgramScorer parsed;std::string line;
 while(std::getline(in,line)){
  if(line.empty()||line[0]=='#')continue;
  std::vector<std::string> cols;std::stringstream ss(line);std::string field;
  while(std::getline(ss,field,'\t'))cols.push_back(field);
  if(cols.size()!=4)return false;
  std::uint32_t count=0;if(!parse_u32_strict(cols[3],count))return false;
  if(cols[0]=="L")parsed.add_left_bigram(cols[1],cols[2],count);
  else if(cols[0]=="R")parsed.add_right_bigram(cols[2],cols[1],count);
  else if(cols[0]=="U")parsed.add_unigram(cols[2],count);
  else return false;
 }
 if(!in.eof())return false;*this=std::move(parsed);return true;
}
double MaskedNgramScorer::score(const std::string&c,const std::vector<std::string>&l,const std::vector<std::string>&r)const{
 constexpr double alpha=.5;
 const double vocab=static_cast<double>(std::max<std::size_t>(1,unigram_counts_.size()));
 auto u=unigram_counts_.find(c);
 const double uc=u==unigram_counts_.end()?0.0:static_cast<double>(u->second);
 double result=std::log((uc+alpha)/(static_cast<double>(unigram_total_)+alpha*vocab));
 if(!l.empty()){
  std::string context=context_key(l);
  auto i=left_counts_.find(key(context,c));
  if(i==left_counts_.end()&&l.size()>1){context=l.back();i=left_counts_.find(key(context,c));}
  auto total=left_context_totals_.find(context);
  const double denominator=total==left_context_totals_.end()?0.0:static_cast<double>(total->second);
  const double count=i==left_counts_.end()?0.0:static_cast<double>(i->second);
  result+=std::log((count+alpha)/(denominator+alpha*vocab));
 }
 if(!r.empty()){
  std::vector<std::string> window(r.begin(),r.begin()+static_cast<std::ptrdiff_t>(std::min<std::size_t>(2,r.size())));
  std::string context=context_key(window);
  auto i=right_counts_.find(key(c,context));
  if(i==right_counts_.end()&&window.size()>1){context=window.front();i=right_counts_.find(key(c,context));}
  auto total=right_context_totals_.find(context);
  const double denominator=total==right_context_totals_.end()?0.0:static_cast<double>(total->second);
  const double count=i==right_counts_.end()?0.0:static_cast<double>(i->second);
  result+=std::log((count+alpha)/(denominator+alpha*vocab));
 }
 return result;
}

bool LexiconTrie::add(const std::string& reading,const LatticeCandidate& candidate){
 if(reading.empty()||!valid_utf8_impl(reading)||!valid_utf8_impl(candidate.id)||!valid_utf8_impl(candidate.surface)||candidate.id.empty()||candidate.surface.empty())return false;
 auto chars=utf8_chars(reading);std::size_t node=0;
 for(const auto&ch:chars){auto it=nodes_[node].next.find(ch);if(it==nodes_[node].next.end()){
  std::size_t next=nodes_.size();nodes_[node].next.emplace(ch,next);nodes_.push_back(Node{});node=next;
 }else node=it->second;}
 auto& values=nodes_[node].values;
 auto duplicate=std::find_if(values.begin(),values.end(),[&](const LatticeCandidate&v){return v.id==candidate.id&&v.surface==candidate.surface;});
 if(duplicate!=values.end())return false;
 values.push_back(candidate);++entry_count_;return true;
}
bool LexiconTrie::load_tsv(const std::string& path,std::size_t* loaded){
 std::ifstream in(path);if(!in)return false;
 LexiconTrie parsed;std::string line;std::size_t count=0;
 while(std::getline(in,line)){
  if(line.empty()||line[0]=='#')continue;
  std::vector<std::string> cols;std::stringstream ss(line);std::string field;
  while(std::getline(ss,field,'\t'))cols.push_back(field);
  if(cols.size()<5||cols.size()>12)return false;
  LatticeCandidate candidate;candidate.id=cols.size()>5&&!cols[5].empty()?cols[5]:cols[4];candidate.surface=cols[1];
  if(cols.size()>6&&!cols[6].empty()&&!parse_i32_strict(cols[6],candidate.word_cost))return false;
  if(cols.size()>7&&!cols[7].empty()&&!parse_i32_strict(cols[7],candidate.connection_cost))return false;
  if(cols.size()>8&&!cols[8].empty()&&!parse_i32_strict(cols[8],candidate.left_id,true))return false;
  if(cols.size()>9&&!cols[9].empty()&&!parse_i32_strict(cols[9],candidate.right_id,true))return false;
  candidate.pos=cols.size()>3?cols[3]:std::string{};
  if(cols.size()>10)candidate.inflection_type=cols[10];
  if(cols.size()>11)candidate.inflection_form=cols[11];
  if(!parsed.add(cols[0],candidate))return false;++count;
 }
 if(!in.eof())return false;
 *this=std::move(parsed);if(loaded)*loaded=count;return true;
}
std::vector<LatticeEdge> LexiconTrie::build_lattice(const std::string& reading,std::size_t max_matches,bool fallback)const{
 std::vector<LatticeEdge> out;if(!valid_utf8_impl(reading))return out;auto chars=utf8_chars(reading);if(chars.empty()||max_matches==0)return out;
 for(std::size_t start=0;start<chars.size();++start){std::size_t node=0,matches=0;
  for(std::size_t end=start;end<chars.size();++end){auto it=nodes_[node].next.find(chars[end]);if(it==nodes_[node].next.end())break;node=it->second;
   if(!nodes_[node].values.empty()){LatticeEdge edge;edge.start=start;edge.end=end+1;edge.candidates=nodes_[node].values;
    std::stable_sort(edge.candidates.begin(),edge.candidates.end(),[](const LatticeCandidate&a,const LatticeCandidate&b){return static_cast<std::int64_t>(a.word_cost)+a.connection_cost<static_cast<std::int64_t>(b.word_cost)+b.connection_cost;});
    if(edge.candidates.size()>8)edge.candidates.resize(8);out.push_back(std::move(edge));if(++matches>=max_matches)break;
   }
  }
  if(fallback&&matches==0)out.push_back(LatticeEdge{start,start+1,{LatticeCandidate{"kana:"+chars[start],chars[start],10000,0}}});
 }return out;
}

static bool load_correction_rules_file(const std::string& path,std::vector<CorrectionRule>& rules){
 std::ifstream in(path);if(!in)return false;
 std::vector<CorrectionRule> parsed;std::set<std::string> ids;std::string line;
 while(std::getline(in,line)){
  if(line.empty()||line[0]=='#')continue;
  std::vector<std::string> cols;std::stringstream ss(line);std::string field;
  while(std::getline(ss,field,'\t'))cols.push_back(field);
  if(cols.size()!=5||cols[2].empty()||cols[0].size()>256||cols[1].size()>256||cols[2].size()>128||parsed.size()>=256||!ids.insert(cols[2]).second)return false;
  if(!is_valid_utf8(cols[0])||!is_valid_utf8(cols[1])||!is_valid_utf8(cols[2]))return false;
  CorrectionKind kind;
  if(cols[3]=="insert")kind=CorrectionKind::Insert;
  else if(cols[3]=="delete")kind=CorrectionKind::Delete;
  else if(cols[3]=="replace")kind=CorrectionKind::Replace;
  else if(cols[3]=="duplicate")kind=CorrectionKind::Duplicate;
  else if(cols[3]=="phonetic")kind=CorrectionKind::Phonetic;
  else return false;
  if((kind==CorrectionKind::Insert&&(cols[0].size()!=0||cols[1].empty()))||
     (kind==CorrectionKind::Delete&&(cols[0].empty()||!cols[1].empty()))||
     ((kind==CorrectionKind::Replace||kind==CorrectionKind::Phonetic)&&(cols[0].empty()||cols[1].empty())))return false;
  std::size_t used=0;double cost=0.0;
  try{cost=std::stod(cols[4],&used);}catch(...){return false;}
  if(used!=cols[4].size()||!std::isfinite(cost)||cost<0.0)return false;
  parsed.push_back({cols[0],cols[1],cols[2],kind,cost});
 }
 if(!in.eof())return false;rules=std::move(parsed);return true;
}

bool load_language_pack(const std::string& directory,LexiconTrie& lexicon,MaskedNgramScorer& scorer,std::size_t* loaded,ConnectionModel* connections,CorrectionOptions* corrections){
 std::string base=directory.empty()?std::string("."):directory;
 if(!verify_pack_manifest(base))return false;
 if(base.back()!='/')base+='/';
 LexiconTrie parsed_lexicon;MaskedNgramScorer parsed_scorer;std::size_t count=0;
 if(!parsed_lexicon.load_tsv(base+"lexicon.tsv",&count)||!parsed_scorer.load_tsv(base+"ngram.tsv"))return false;
 ConnectionModel parsed_connections;std::ifstream conn_file(base+"connections.tsv");if(conn_file&&!parsed_connections.load_tsv(base+"connections.tsv"))return false;
 CorrectionOptions parsed_corrections;std::ifstream correction_file(base+"correction_rules.tsv");
 if(correction_file&&!load_correction_rules_file(base+"correction_rules.tsv",parsed_corrections.rules))return false;
 lexicon=std::move(parsed_lexicon);scorer=std::move(parsed_scorer);if(connections)*connections=std::move(parsed_connections);
 if(corrections)*corrections=std::move(parsed_corrections);if(loaded)*loaded=count;return true;
}
std::vector<DecodedPath> decode_reading(const std::string& reading,const LexiconTrie& lexicon,const MaskedNgramScorer& scorer,const DecoderOptions& options,const ConnectionModel* connections){
 if(!valid_utf8_impl(reading))return {};auto chars=utf8_chars(reading);if(chars.empty())return {};auto lattice=lexicon.build_lattice(reading,16,true);MorphologyDecoder decoder;return decoder.decode(chars.size(),lattice,scorer,{},options,connections);
}
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

StreamingSession::StreamingSession(std::size_t limit):max_reading_codepoints_(limit?limit:1){}
bool StreamingSession::append(const std::string& chunk,const LexiconTrie& lexicon,const MaskedNgramScorer& scorer,StreamingSnapshot& snapshot,const DecoderOptions& options,const ConnectionModel* connections){
 if(chunk.empty()||!valid_utf8_impl(chunk))return false;auto current=utf8_chars(reading_),incoming=utf8_chars(chunk);if(current.size()+incoming.size()>max_reading_codepoints_)return false;
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
std::string render_ambiguity(const std::vector<Candidate>&c,bool other){std::ostringstream o;o<<'[';bool f=true;for(const auto&x:c){if(x.surface.empty())continue;if(!f)o<<'/';o<<x.surface;f=false;}if(other){if(!f)o<<'/';o<<"other";}o<<']';return o.str();}
AmbiguityResult MaskedNgramScorer::resolve(const std::vector<Candidate>&c,const std::vector<std::string>&l,const std::vector<std::string>&r,OutputMode m,double ms,double mm,bool other,const std::string&original)const{
 AmbiguityResult x;x.ranked=c;if(x.ranked.size()>8)x.ranked.resize(8);std::stable_sort(x.ranked.begin(),x.ranked.end(),[&](const Candidate&a,const Candidate&b){return score(a.id,l,r)>score(b.id,l,r);});
 if(x.ranked.empty())return x;x.top_score=score(x.ranked[0].id,l,r);double second=x.ranked.size()>1?score(x.ranked[1].id,l,r):x.top_score-100.0;x.margin=x.top_score-second;x.ambiguous=x.top_score<ms||x.margin<mm;
 if(m==OutputMode::Conservative&&x.ambiguous)x.output=original.empty()?x.ranked[0].id:original;else if((m==OutputMode::Ambiguous||m==OutputMode::Diagnostic)&&x.ambiguous)x.output=render_ambiguity(x.ranked,other);else x.output=x.ranked[0].surface;return x;
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
bool DependencyGraph::add(HypothesisNode n){if(n.id.empty()||nodes_.count(n.id))return false;for(const auto&d:n.dependencies){auto i=nodes_.find(d);if(i==nodes_.end()||static_cast<int>(i->second.layer)>static_cast<int>(n.layer))return false;}nodes_.emplace(n.id,std::move(n));return true;}
std::vector<std::string> DependencyGraph::invalidate_dependents(const std::string&id){if(!nodes_.count(id))return{};std::set<std::string>a;bool changed=true;while(changed){changed=false;for(auto&e:nodes_){if(e.first==id||a.count(e.first))continue;for(const auto&d:e.second.dependencies)if(d==id||a.count(d)){a.insert(e.first);changed=true;break;}}}std::vector<std::string>v(a.begin(),a.end());for(const auto&k:v){auto&n=nodes_.at(k);n.valid=false;++n.revision;}return v;}
const HypothesisNode*DependencyGraph::find(const std::string&id)const{auto i=nodes_.find(id);return i==nodes_.end()?nullptr:&i->second;}
}
