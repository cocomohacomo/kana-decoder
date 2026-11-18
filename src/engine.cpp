#include "kana_decoder/engine.hpp"
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
void ConnectionModel::set_cost(std::int32_t right_id,std::int32_t left_id,std::int32_t value){costs_[pair_key(right_id,left_id)]=value;}
std::int32_t ConnectionModel::cost(std::int32_t right_id,std::int32_t left_id)const{auto it=costs_.find(pair_key(right_id,left_id));return it==costs_.end()?0:it->second;}
bool ConnectionModel::load_tsv(const std::string& path){
 std::ifstream in(path);if(!in)return false;ConnectionModel parsed;std::string line;
 while(std::getline(in,line)){if(line.empty()||line[0]=='#')continue;std::stringstream ss(line);std::string a,b,v,extra;
  if(!std::getline(ss,a,'\t')||!std::getline(ss,b,'\t')||!std::getline(ss,v,'\t')||std::getline(ss,extra,'\t'))return false;
  std::int32_t x=0,y=0,z=0;if(!parse_i32_strict(a,x,true)||!parse_i32_strict(b,y,true)||!parse_i32_strict(v,z))return false;parsed.set_cost(x,y,z);
 }
 if(!in.eof())return false;*this=std::move(parsed);return true;
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
  if(cols.size()<5||cols.size()>10)return false;
  LatticeCandidate candidate;candidate.id=cols.size()>5&&!cols[5].empty()?cols[5]:cols[4];candidate.surface=cols[1];
  if(cols.size()>6&&!cols[6].empty()&&!parse_i32_strict(cols[6],candidate.word_cost))return false;
  if(cols.size()>7&&!cols[7].empty()&&!parse_i32_strict(cols[7],candidate.connection_cost))return false;
  if(cols.size()>8&&!cols[8].empty()&&!parse_i32_strict(cols[8],candidate.left_id,true))return false;
  if(cols.size()>9&&!cols[9].empty()&&!parse_i32_strict(cols[9],candidate.right_id,true))return false;
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

bool load_language_pack(const std::string& directory,LexiconTrie& lexicon,MaskedNgramScorer& scorer,std::size_t* loaded,ConnectionModel* connections){
 std::string base=directory.empty()?std::string("."):directory;
 if(base.back()!='/')base+='/';
 LexiconTrie parsed_lexicon;MaskedNgramScorer parsed_scorer;std::size_t count=0;
 if(!parsed_lexicon.load_tsv(base+"lexicon.tsv",&count)||!parsed_scorer.load_tsv(base+"ngram.tsv"))return false;
 ConnectionModel parsed_connections;std::ifstream conn_file(base+"connections.tsv");if(conn_file&&!parsed_connections.load_tsv(base+"connections.tsv"))return false;
 lexicon=std::move(parsed_lexicon);scorer=std::move(parsed_scorer);if(connections)*connections=std::move(parsed_connections);if(loaded)*loaded=count;return true;
}
std::vector<DecodedPath> decode_reading(const std::string& reading,const LexiconTrie& lexicon,const MaskedNgramScorer& scorer,const DecoderOptions& options,const ConnectionModel* connections){
 if(!valid_utf8_impl(reading))return {};auto chars=utf8_chars(reading);if(chars.empty())return {};auto lattice=lexicon.build_lattice(reading,16,true);MorphologyDecoder decoder;return decoder.decode(chars.size(),lattice,scorer,{},options,connections);
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
std::vector<DecodedPath> MorphologyDecoder::decode(std::size_t length,
 const std::vector<LatticeEdge>& lattice,const MaskedNgramScorer& scorer,
 const std::vector<std::string>& right_context,const DecoderOptions& opt,const ConnectionModel* connections) const {
 if(length==0||opt.beam_width==0||opt.max_edges_per_position==0||
    opt.max_candidates_per_edge==0||opt.max_paths==0)return {};
 // Hard ceilings keep caller-supplied options from defeating bounded-work guarantees.
 const std::size_t beam_limit=std::min<std::size_t>(opt.beam_width,8);
 const std::size_t edge_limit=std::min<std::size_t>(opt.max_edges_per_position,16);
 const std::size_t candidate_limit=std::min<std::size_t>(opt.max_candidates_per_edge,8);
 const std::size_t path_limit=std::min<std::size_t>(opt.max_paths,8);
 std::vector<std::vector<const LatticeEdge*>> at(length+1);
 for(const auto& edge:lattice){
  if(edge.start>=edge.end||edge.end>length||edge.candidates.empty())continue;
  at[edge.start].push_back(&edge);
 }
 struct State { DecodedPath path; std::vector<std::string> history; std::int32_t last_right_id=0; bool has_last=false; };
 std::vector<std::vector<State>> beams(length+1);
 beams[0].push_back(State{});
 auto prune=[&](std::vector<State>& states){
  std::stable_sort(states.begin(),states.end(),[](const State&a,const State&b){return a.path.score>b.path.score;});
  if(states.size()>beam_limit)states.resize(beam_limit);
 };
 for(std::size_t pos=0;pos<length;++pos){
  if(beams[pos].empty())continue;
  auto edges=at[pos];
  std::stable_sort(edges.begin(),edges.end(),[](const LatticeEdge*a,const LatticeEdge*b){
   if(a->end!=b->end)return a->end<b->end;
   return a->candidates.size()<b->candidates.size();
  });
  if(edges.size()>edge_limit)edges.resize(edge_limit);
  for(const auto* edge:edges){
   auto candidates=edge->candidates;
   std::stable_sort(candidates.begin(),candidates.end(),[](const LatticeCandidate&a,const LatticeCandidate&b){
    return static_cast<std::int64_t>(a.word_cost)+a.connection_cost<
           static_cast<std::int64_t>(b.word_cost)+b.connection_cost;
   });
   if(candidates.size()>candidate_limit)candidates.resize(candidate_limit);
   for(const auto& state:beams[pos])for(const auto& candidate:candidates){
    if(candidate.id.empty()||candidate.surface.empty())continue;
    State next=state;
    const std::int32_t transition=(connections&&state.has_last)?connections->cost(state.last_right_id,candidate.left_id):0;
    const double morph=-static_cast<double>(candidate.word_cost)-
                       static_cast<double>(candidate.connection_cost)-static_cast<double>(transition);
    const double ctx=scorer.score(candidate.id,state.history,right_context)*opt.context_weight;
    next.path.score+=morph+ctx;
    next.path.tokens.push_back(DecodedToken{edge->start,edge->end,
      Candidate{candidate.id,candidate.surface},morph,ctx});
    next.history.push_back(candidate.id);next.last_right_id=candidate.right_id;next.has_last=true;
    if(next.history.size()>2)next.history.erase(next.history.begin());
    beams[edge->end].push_back(std::move(next));
   }
   prune(beams[edge->end]);
  }
 }
 std::vector<DecodedPath> out;
 for(auto& state:beams[length])out.push_back(std::move(state.path));
 std::stable_sort(out.begin(),out.end(),[](const DecodedPath&a,const DecodedPath&b){return a.score>b.score;});
 if(out.size()>path_limit)out.resize(path_limit);
 return out;
}
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
