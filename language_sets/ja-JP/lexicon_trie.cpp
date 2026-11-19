#include "kana_decoder/japanese_decoder.hpp"
#include <algorithm>
#include <charconv>
#include <fstream>
#include <sstream>
namespace {
bool valid_utf8_impl(const std::string& s){return kana_decoder::is_valid_utf8(s);}
std::vector<std::string> utf8_chars(const std::string&s){std::vector<std::string>o;if(!valid_utf8_impl(s))return o;for(std::size_t i=0;i<s.size();){unsigned char c=static_cast<unsigned char>(s[i]);std::size_t n=c<0x80?1:(c&0xe0)==0xc0?2:(c&0xf0)==0xe0?3:4;o.push_back(s.substr(i,n));i+=n;}return o;}
bool parse_i32_strict(const std::string& text,std::int32_t& value,bool nonnegative=false){if(text.empty())return false;auto r=std::from_chars(text.data(),text.data()+text.size(),value);return r.ec==std::errc{}&&r.ptr==text.data()+text.size()&&(!nonnegative||value>=0);}
std::int64_t candidate_cost(const kana_decoder::LatticeCandidate& c){return static_cast<std::int64_t>(c.word_cost)+c.connection_cost;}
std::vector<kana_decoder::LatticeCandidate> retain_surface_diverse(const std::vector<kana_decoder::LatticeCandidate>& source,std::size_t limit){
 std::vector<const kana_decoder::LatticeCandidate*> ranked;for(const auto& c:source)ranked.push_back(&c);
 std::stable_sort(ranked.begin(),ranked.end(),[](const auto* a,const auto* b){return candidate_cost(*a)<candidate_cost(*b);});
 std::vector<kana_decoder::LatticeCandidate> selected;std::vector<std::string> surfaces;selected.reserve(std::min(limit,ranked.size()));
 for(const auto* c:ranked){if(selected.size()>=limit)break;if(std::find(surfaces.begin(),surfaces.end(),c->surface)!=surfaces.end())continue;surfaces.push_back(c->surface);selected.push_back(*c);}
 if(selected.size()<limit)for(const auto* c:ranked){if(selected.size()>=limit)break;if(std::none_of(selected.begin(),selected.end(),[&](const auto& x){return x.id==c->id&&x.surface==c->surface;}))selected.push_back(*c);}
 std::stable_sort(selected.begin(),selected.end(),[](const auto& a,const auto& b){return candidate_cost(a)<candidate_cost(b);});return selected;
}
std::vector<std::size_t> spread_indices(std::size_t count,std::size_t limit){
 std::vector<std::size_t> out;if(!limit||!count)return out;if(count<=limit){for(std::size_t i=0;i<count;++i)out.push_back(i);return out;}if(limit==1){out.push_back(count-1);return out;}for(std::size_t i=0;i<limit;++i)out.push_back(i*(count-1)/(limit-1));return out;
}
}
namespace kana_decoder {
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
  if (!parsed.add(cols[0], candidate)) return false;
  ++count;
 }
 if(!in.eof())return false;
 *this=std::move(parsed);if(loaded)*loaded=count;return true;
}
void LexiconTrie::build_candidate_pos_index() {
 candidate_pos_.clear();
 for(const auto& node:nodes_) {
  for(const auto& candidate:node.values) {
   if(candidate.id.empty()||candidate.pos.empty()) continue;
   auto found=candidate_pos_.find(candidate.id);
   if(found==candidate_pos_.end()) candidate_pos_.emplace(candidate.id,candidate.pos);
   else if(found->second!=candidate.pos) found->second.clear();
  }
 }
}
std::string LexiconTrie::candidate_pos(const std::string& candidate_id) const {
 auto found=candidate_pos_.find(candidate_id);
 return found==candidate_pos_.end()?std::string{}:found->second;
}
std::vector<LatticeEdge> LexiconTrie::build_lattice(
 const std::string& reading,std::size_t max_matches,bool fallback,
 std::vector<LatticePositionTrace>* trace,std::size_t max_total_edges,
 std::size_t max_total_candidates,bool* resource_limit_exceeded) const {
 if (trace) trace->clear();
 if (resource_limit_exceeded) *resource_limit_exceeded=false;
 max_matches=std::min<std::size_t>(max_matches,16);
 max_total_edges=std::min<std::size_t>(max_total_edges,4096);
 max_total_candidates=std::min<std::size_t>(max_total_candidates,32768);
 std::vector<LatticeEdge> out;
 if(max_total_edges==0||max_total_candidates==0){
  if(resource_limit_exceeded)*resource_limit_exceeded=true;
  return out;
 }
 std::size_t retained_candidates=0;
 if (!valid_utf8_impl(reading)) return out;
 if(reading.size()>1024){
  if(resource_limit_exceeded)*resource_limit_exceeded=true;
  return out;
 }
 auto chars = utf8_chars(reading);
 if (chars.empty()) return out;
 if(chars.size()>256){
  if(resource_limit_exceeded)*resource_limit_exceeded=true;
  return out;
 }
 for (std::size_t start = 0; start < chars.size(); ++start) {
  if(out.size()>=max_total_edges){
   if(resource_limit_exceeded)*resource_limit_exceeded=true;
   break;
  }
  LatticePositionTrace position_trace;
  position_trace.start = start;
  position_trace.fallback_enabled = fallback;
  std::vector<LatticeEdge> available_edges;std::vector<std::size_t> raw_counts;std::size_t node=0;
  if(max_matches)for(std::size_t end=start;end<chars.size();++end){
   auto it=nodes_[node].next.find(chars[end]);if(it==nodes_[node].next.end())break;node=it->second;
   if(!nodes_[node].values.empty()){position_trace.available_candidate_count+=nodes_[node].values.size();available_edges.push_back(LatticeEdge{start,end+1,{}});raw_counts.push_back(nodes_[node].values.size());available_edges.back().candidates=retain_surface_diverse(nodes_[node].values,std::min<std::size_t>(8,nodes_[node].values.size()));}
  }
  position_trace.available_dictionary_match_count=available_edges.size();
  const auto selected_indices=spread_indices(available_edges.size(),max_matches);std::size_t matches=0;
  for(const auto index:selected_indices){
   if(out.size()>=max_total_edges||retained_candidates>=max_total_candidates){if(resource_limit_exceeded)*resource_limit_exceeded=true;break;}
   auto edge=std::move(available_edges[index]);const auto take=std::min(edge.candidates.size(),max_total_candidates-retained_candidates);
   if(take<edge.candidates.size()&&resource_limit_exceeded)*resource_limit_exceeded=true;
   if(take<edge.candidates.size())edge.candidates=retain_surface_diverse(edge.candidates,take);
   if(edge.candidates.empty())break;
   const auto kept=edge.candidates.size();position_trace.retained_candidate_count+=kept;retained_candidates+=kept;
   if(trace){std::vector<std::string> surfaces;for(const auto& c:edge.candidates)if(std::find(surfaces.begin(),surfaces.end(),c.surface)==surfaces.end())surfaces.push_back(c.surface);
    LatticeMatchTrace mt;mt.start=edge.start;mt.end=edge.end;mt.available_candidate_count=raw_counts[index];mt.retained_candidate_count=kept;mt.unique_surface_count=surfaces.size();mt.candidates=edge.candidates;position_trace.dictionary_matches.push_back(std::move(mt));}
   out.push_back(std::move(edge));++matches;
  }
  position_trace.dictionary_match_count=matches;
  position_trace.pruned_dictionary_match_count=position_trace.available_dictionary_match_count-matches;
  position_trace.pruned_candidate_count=position_trace.available_candidate_count-position_trace.retained_candidate_count;
  if(fallback&&position_trace.available_dictionary_match_count==0&&max_matches){out.push_back(LatticeEdge{start,start+1,{LatticeCandidate{"kana:"+chars[start],chars[start],10000,0,0,0,{},{},{}}}});position_trace.fallback_generated=true;}
  else if(fallback&&position_trace.available_dictionary_match_count)position_trace.fallback_suppressed_by_dictionary=true;
  if (trace) trace->push_back(std::move(position_trace));
 }
 return out;
}

}
