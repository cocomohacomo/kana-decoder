#include "kana_decoder/japanese_decoder.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
namespace {
void sat_add(std::uint64_t& target,std::uint64_t value){target=std::numeric_limits<std::uint64_t>::max()-target<value?std::numeric_limits<std::uint64_t>::max():target+value;}
bool parse_i32_strict(const std::string& text,std::int32_t& value,bool nonnegative=false){if(text.empty())return false;auto r=std::from_chars(text.data(),text.data()+text.size(),value);return r.ec==std::errc{}&&r.ptr==text.data()+text.size()&&(!nonnegative||value>=0);}
bool parse_u32_strict(const std::string& text,std::uint32_t& value){if(text.empty())return false;auto r=std::from_chars(text.data(),text.data()+text.size(),value);return r.ec==std::errc{}&&r.ptr==text.data()+text.size();}
}
namespace kana_decoder {
std::string MaskedNgramScorer::key(const std::string&a,const std::string&b){return a+"\x1f"+b;}
std::string MaskedNgramScorer::context_key(const std::vector<std::string>&c){std::string o;std::size_t b=c.size()>3?c.size()-3:0;for(std::size_t i=b;i<c.size();++i){if(!o.empty())o+="\x1e";o+=c[i];}return o;}
void MaskedNgramScorer::add_left_bigram(const std::string&l,const std::string&c,std::uint32_t n){sat_add(left_counts_[key(l,c)],n);sat_add(left_context_totals_[l],n);}
void MaskedNgramScorer::add_right_bigram(const std::string&c,const std::string&r,std::uint32_t n){sat_add(right_counts_[key(c,r)],n);sat_add(right_context_totals_[r],n);}
void MaskedNgramScorer::add_left_ngram(const std::vector<std::string>&l,const std::string&c,std::uint32_t n){auto ctx=context_key(l);sat_add(left_counts_[key(ctx,c)],n);sat_add(left_context_totals_[ctx],n);}
void MaskedNgramScorer::add_left_skipgram(const std::string&l,const std::string&c,std::uint32_t n){if(l.empty()||c.empty())return;sat_add(skip_counts_[key(l,c)],n);sat_add(skip_context_totals_[l],n);}
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
 if (!in.eof()) return false;
 *this = std::move(parsed);
 return true;
}
bool MaskedNgramScorer::set_smoothing_alpha(double alpha) {
 if (!std::isfinite(alpha) || alpha <= 0.0 || alpha > 100.0) return false;
 smoothing_alpha_ = alpha;
 return true;
}
double MaskedNgramScorer::contextual_score(const std::string& c,const std::vector<std::string>& l,const std::vector<std::string>& r) const {
 const double alpha=smoothing_alpha_;
 const double vocab=static_cast<double>(std::max<std::size_t>(1,unigram_counts_.size()));
 double left_sum=0.0,left_weight=0.0;
 auto add_order=[&](const std::vector<std::string>& history,double weight) {
  if(history.empty()) return;
  const auto context=context_key(history);
  const auto count=left_counts_.find(key(context,c));
  const auto total=left_context_totals_.find(context);
  if(count==left_counts_.end()||total==left_context_totals_.end()) return;
  left_sum+=weight*std::log((static_cast<double>(count->second)+alpha)/(static_cast<double>(total->second)+alpha*vocab));
  left_weight+=weight;
 };
 if(!l.empty()) {
  const std::size_t n=l.size();
  if(n>=3) add_order({l[n-3],l[n-2],l[n-1]},0.50);
  if(n>=2) add_order({l[n-2],l[n-1]},0.30);
  add_order({l[n-1]},0.15);
  if(n>=2) {
   const auto skip=skip_counts_.find(key(l[n-2],c));
   const auto total=skip_context_totals_.find(l[n-2]);
   if(skip!=skip_counts_.end()&&total!=skip_context_totals_.end()) {
    left_sum+=0.05*std::log((static_cast<double>(skip->second)+alpha)/(static_cast<double>(total->second)+alpha*vocab));
    left_weight+=0.05;
   }
  }
 }
 double result=left_weight>0.0?left_sum/left_weight:0.0;
 if(!r.empty()) {
  std::vector<std::string> window(r.begin(),r.begin()+static_cast<std::ptrdiff_t>(std::min<std::size_t>(3,r.size())));
  const auto context=context_key(window);
  const auto count=right_counts_.find(key(c,context));
  const auto total=right_context_totals_.find(context);
  const double denominator=total==right_context_totals_.end()?0.0:static_cast<double>(total->second);
  const double observed=count==right_counts_.end()?0.0:static_cast<double>(count->second);
  result+=std::log((observed+alpha)/(denominator+alpha*vocab));
 }
 return result;
}
bool MaskedNgramScorer::has_left_context_evidence(const std::string& candidate,const std::vector<std::string>& left) const {
 if(candidate.empty()||left.empty()) return false;
 if(left.size()>=3&&left_counts_.find(key(context_key({left[left.size()-3],left[left.size()-2],left.back()}),candidate))!=left_counts_.end()) return true;
 if(left.size()>=2&&left_counts_.find(key(context_key({left[left.size()-2],left.back()}),candidate))!=left_counts_.end()) return true;
 if(left_counts_.find(key(context_key({left.back()}),candidate))!=left_counts_.end()) return true;
 return left.size()>1&&skip_counts_.find(key(left[left.size()-2],candidate))!=skip_counts_.end();
}
bool MaskedNgramScorer::has_right_context_evidence(const std::string& candidate,
    const std::vector<std::string>& right) const {
 if(candidate.empty()||right.empty()) return false;
 const std::vector<std::string> window(right.begin(),
   right.begin()+static_cast<std::ptrdiff_t>(std::min<std::size_t>(3,right.size())));
 return right_counts_.find(key(candidate,context_key(window)))!=right_counts_.end();
}
double MaskedNgramScorer::skip_contextual_score(const std::string& candidate,const std::vector<std::string>& left) const {
 if(candidate.empty()||left.size()<2) return 0.0;
 const auto context=left[left.size()-2];
 const auto count=skip_counts_.find(key(context,candidate));
 const auto total=skip_context_totals_.find(context);
 if(count==skip_counts_.end()||total==skip_context_totals_.end()) return 0.0;
 const double alpha=smoothing_alpha_;
 const double vocab=static_cast<double>(std::max<std::size_t>(1,unigram_counts_.size()));
 return std::log((static_cast<double>(count->second)+alpha)/
   (static_cast<double>(total->second)+alpha*vocab));
}
double MaskedNgramScorer::score(const std::string&c,const std::vector<std::string>&l,const std::vector<std::string>&r)const{
 const double alpha=smoothing_alpha_;
 const double vocab=static_cast<double>(std::max<std::size_t>(1,unigram_counts_.size()));
 auto u=unigram_counts_.find(c);
 const double uc=u==unigram_counts_.end()?0.0:static_cast<double>(u->second);
 const double unigram=std::log((uc+alpha)/(static_cast<double>(unigram_total_)+alpha*vocab));
 return unigram+contextual_score(c,l,r);
}

AmbiguityResult MaskedNgramScorer::resolve(const std::vector<Candidate>&c,const std::vector<std::string>&l,const std::vector<std::string>&r,OutputMode m,double ms,double mm,bool other,const std::string&original)const{
 AmbiguityResult x;x.ranked=c;if(x.ranked.size()>8)x.ranked.resize(8);std::stable_sort(x.ranked.begin(),x.ranked.end(),[&](const Candidate&a,const Candidate&b){return score(a.id,l,r)>score(b.id,l,r);});
 if (x.ranked.empty()) return x;
 x.top_score = score(x.ranked[0].id, l, r);
 double second = x.ranked.size() > 1 ? score(x.ranked[1].id, l, r) : x.top_score - 100.0;
 x.margin = x.top_score - second;
 x.ambiguous = x.top_score < ms || x.margin < mm;
 if(m==OutputMode::Conservative&&x.ambiguous)x.output=original.empty()?x.ranked[0].id:original;else if((m==OutputMode::Ambiguous||m==OutputMode::Diagnostic)&&x.ambiguous)x.output=render_ambiguity(x.ranked,other);else x.output=x.ranked[0].surface;return x;
}
}
