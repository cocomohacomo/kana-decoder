#include "kana_decoder/engine.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>
namespace {
std::vector<std::string> utf8_chars(const std::string&s){std::vector<std::string>o;for(std::size_t i=0;i<s.size();){unsigned char c=static_cast<unsigned char>(s[i]);std::size_t n=c<0x80?1:(c&0xE0)==0xC0?2:(c&0xF0)==0xE0?3:(c&0xF8)==0xF0?4:1;if(i+n>s.size())n=1;o.push_back(s.substr(i,n));i+=n;}return o;}
std::string join_utf8(const std::vector<std::string>&v){std::string o;for(const auto&c:v)o+=c;return o;}
}
namespace kana_decoder {
std::string MaskedNgramScorer::key(const std::string&a,const std::string&b){return a+"\x1f"+b;}
std::string MaskedNgramScorer::context_key(const std::vector<std::string>&c){std::string o;std::size_t b=c.size()>2?c.size()-2:0;for(std::size_t i=b;i<c.size();++i){if(!o.empty())o+="\x1e";o+=c[i];}return o;}
void MaskedNgramScorer::add_left_bigram(const std::string&l,const std::string&c,std::uint32_t n){left_counts_[key(l,c)]+=n;}
void MaskedNgramScorer::add_right_bigram(const std::string&c,const std::string&r,std::uint32_t n){right_counts_[key(c,r)]+=n;}
void MaskedNgramScorer::add_left_ngram(const std::vector<std::string>&l,const std::string&c,std::uint32_t n){left_counts_[key(context_key(l),c)]+=n;}
void MaskedNgramScorer::add_right_ngram(const std::string&c,const std::vector<std::string>&r,std::uint32_t n){right_counts_[key(c,context_key(r))]+=n;}
void MaskedNgramScorer::add_unigram(const std::string&c,std::uint32_t n){unigram_counts_[c]+=n;}
double MaskedNgramScorer::score(const std::string&c,const std::vector<std::string>&l,const std::vector<std::string>&r)const{
 constexpr double a=.5;auto u=unigram_counts_.find(c);double uc=u==unigram_counts_.end()?0:u->second;
 double s=std::log((uc+a)/(static_cast<double>(unigram_counts_.size())+10*a));
 if(!l.empty()){auto i=left_counts_.find(key(context_key(l),c));s+=std::log(((i==left_counts_.end()?0.0:i->second)+a)/(uc+10*a));}
 if(!r.empty()){std::vector<std::string>b(r.begin(),r.begin()+static_cast<std::ptrdiff_t>(std::min<std::size_t>(2,r.size())));auto i=right_counts_.find(key(c,context_key(b)));s+=std::log(((i==right_counts_.end()?0.0:i->second)+a)/(uc+10*a));}return s;
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
 auto chars=utf8_chars(input);for(std::size_t i=1;i<chars.size()&&out.size()<o.max_candidates;++i)if(chars[i]==chars[i-1]){auto e=chars;e.erase(e.begin()+static_cast<std::ptrdiff_t>(i));add(join_utf8(e),CorrectionKind::Duplicate,i,chars[i],"","builtin-duplicate-delete",1.0);}
 for(const auto&rule:o.rules){if(out.size()>=o.max_candidates)break;if(rule.from.empty()){if(rule.to.empty())continue;std::size_t pos=0;for(std::size_t i=0;i<=chars.size()&&out.size()<o.max_candidates;++i){std::string p=input;p.insert(pos,rule.to);add(p,rule.kind,pos,"",rule.to,rule.rule_id,rule.cost);if(i<chars.size())pos+=chars[i].size();}continue;}
 std::size_t pos=input.find(rule.from);while(pos!=std::string::npos&&out.size()<o.max_candidates){std::string p=input;p.replace(pos,rule.from.size(),rule.to);add(p,rule.kind,pos,rule.from,rule.to,rule.rule_id,rule.cost);pos=input.find(rule.from,pos+rule.from.size());}}
 return out;
}
bool DependencyGraph::add(HypothesisNode n){if(n.id.empty()||nodes_.count(n.id))return false;for(const auto&d:n.dependencies){auto i=nodes_.find(d);if(i==nodes_.end()||static_cast<int>(i->second.layer)>static_cast<int>(n.layer))return false;}nodes_.emplace(n.id,std::move(n));return true;}
std::vector<std::string> DependencyGraph::invalidate_dependents(const std::string&id){if(!nodes_.count(id))return{};std::set<std::string>a;bool changed=true;while(changed){changed=false;for(auto&e:nodes_){if(e.first==id||a.count(e.first))continue;for(const auto&d:e.second.dependencies)if(d==id||a.count(d)){a.insert(e.first);changed=true;break;}}}std::vector<std::string>v(a.begin(),a.end());for(const auto&k:v){auto&n=nodes_.at(k);n.valid=false;++n.revision;}return v;}
const HypothesisNode*DependencyGraph::find(const std::string&id)const{auto i=nodes_.find(id);return i==nodes_.end()?nullptr:&i->second;}
}
