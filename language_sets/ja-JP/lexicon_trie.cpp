#include "kana_decoder/japanese_decoder.hpp"
#include <algorithm>
#include <charconv>
#include <fstream>
#include <sstream>
#include <zlib.h>
namespace {
bool valid_utf8_impl(const std::string& s){return kana_decoder::is_valid_utf8(s);}
std::size_t utf8_width(unsigned char c){return c<0x80?1:(c&0xe0)==0xc0?2:(c&0xf0)==0xe0?3:4;}
std::uint32_t utf8_codepoint_at(const std::string& text,std::size_t offset){const auto* p=reinterpret_cast<const unsigned char*>(text.data()+offset);const auto n=utf8_width(*p);if(n==1)return p[0];if(n==2)return (static_cast<std::uint32_t>(p[0]&0x1f)<<6)|(p[1]&0x3f);if(n==3)return (static_cast<std::uint32_t>(p[0]&0x0f)<<12)|(static_cast<std::uint32_t>(p[1]&0x3f)<<6)|(p[2]&0x3f);return (static_cast<std::uint32_t>(p[0]&0x07)<<18)|(static_cast<std::uint32_t>(p[1]&0x3f)<<12)|(static_cast<std::uint32_t>(p[2]&0x3f)<<6)|(p[3]&0x3f);}
bool parse_i32_strict(const std::string& text,std::int32_t& value,bool nonnegative=false){if(text.empty())return false;auto r=std::from_chars(text.data(),text.data()+text.size(),value);return r.ec==std::errc{}&&r.ptr==text.data()+text.size()&&(!nonnegative||value>=0);}
std::int64_t candidate_cost(const kana_decoder::LatticeCandidate& c){return static_cast<std::int64_t>(c.word_cost)+c.connection_cost;}
std::vector<const kana_decoder::LatticeCandidate*> retain_surface_diverse_ptr(const std::vector<kana_decoder::LatticeCandidate>& source,std::size_t limit){
 std::vector<const kana_decoder::LatticeCandidate*> selected;selected.reserve(std::min(limit,source.size()));
 auto sort_selected=[&](){std::stable_sort(selected.begin(),selected.end(),[](const auto* a,const auto* b){return candidate_cost(*a)<candidate_cost(*b);});};
 for(const auto& candidate:source){
  const auto* ptr=&candidate;
  auto same=std::find_if(selected.begin(),selected.end(),[&](const auto* item){return item->surface==candidate.surface;});
  if(same!=selected.end()){if(candidate_cost(candidate)<candidate_cost(**same)){*same=ptr;sort_selected();}continue;}
  if(selected.size()<limit){selected.push_back(ptr);sort_selected();}
  else if(limit&&candidate_cost(candidate)<candidate_cost(*selected.back())){selected.back()=ptr;sort_selected();}
 }
 if(selected.size()<limit){
  for(const auto& candidate:source){
   if(selected.size()>=limit)break;
   const auto* ptr=&candidate;
   if(std::find(selected.begin(),selected.end(),ptr)!=selected.end())continue;
   auto where=std::find_if(selected.begin(),selected.end(),[&](const auto* item){return candidate_cost(candidate)<candidate_cost(*item);});
   selected.insert(where,ptr);
  }
 }
 return selected;
}
std::vector<kana_decoder::LatticeCandidate> retain_surface_diverse(const std::vector<kana_decoder::LatticeCandidate>& source,std::size_t limit){
 const auto selected=retain_surface_diverse_ptr(source,limit);
 std::vector<kana_decoder::LatticeCandidate> out;out.reserve(selected.size());
 for(const auto* candidate:selected)out.push_back(*candidate);
 return out;
}
std::vector<std::size_t> spread_indices(std::size_t count,std::size_t limit){
 std::vector<std::size_t> out;if(!limit||!count)return out;if(count<=limit){for(std::size_t i=0;i<count;++i)out.push_back(i);return out;}if(limit==1){out.push_back(count-1);return out;}for(std::size_t i=0;i<limit;++i)out.push_back(i*(count-1)/(limit-1));return out;
}
}
namespace kana_decoder {
bool LexiconTrie::add(const std::string& reading,const LatticeCandidate& candidate){
 if(reading.empty()||!valid_utf8_impl(reading)||!valid_utf8_impl(candidate.id)||!valid_utf8_impl(candidate.surface)||candidate.id.empty()||candidate.surface.empty())return false;
 std::size_t node=0;
 for(std::size_t offset=0;offset<reading.size();offset+=utf8_width(static_cast<unsigned char>(reading[offset]))){const auto cp=utf8_codepoint_at(reading,offset);auto& transitions=nodes_[node].next;auto it=std::lower_bound(transitions.begin(),transitions.end(),cp,[](const auto& edge,std::uint32_t value){return edge.first<value;});if(it==transitions.end()||it->first!=cp){
  std::size_t next=nodes_.size();transitions.insert(it,{cp,next});nodes_.push_back(Node{});node=next;
 }else node=it->second;}
 auto& values=nodes_[node].values;
 auto duplicate=std::find_if(values.begin(),values.end(),[&](const LatticeCandidate&v){return v.id==candidate.id&&v.surface==candidate.surface;});
 if(duplicate!=values.end())return false;
 values.push_back(candidate);++entry_count_;return true;
}
bool LexiconTrie::load_tsv(const std::string& path,std::size_t* loaded){
 std::ifstream probe(path,std::ios::binary);if(!probe)return false;
 unsigned char magic[2]{};probe.read(reinterpret_cast<char*>(magic),2);
 const bool gzip=probe.gcount()==2&&magic[0]==0x1f&&magic[1]==0x8b;probe.close();
 LexiconTrie parsed;std::size_t count=0;std::string line;
 auto parse_line=[&](const std::string& row)->bool{
  if(row.empty()||row[0]=='#')return true;
  std::vector<std::string> cols;std::stringstream ss(row);std::string field;
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
  if(!parsed.add(cols[0],candidate))return false;
  ++count;return true;
 };
 if(gzip){
  gzFile input=gzopen(path.c_str(),"rb");if(!input)return false;
  char buffer[8192];bool ok=true;
  while(ok&&gzgets(input,buffer,sizeof(buffer))){
   line.append(buffer);
   if(!line.empty()&&line.back()=='\n'){line.pop_back();if(!line.empty()&&line.back()=='\r')line.pop_back();ok=parse_line(line);line.clear();}
   else if(line.size()>1024u*1024u)ok=false;
  }
  if(ok&&!line.empty())ok=parse_line(line);
  int code=Z_OK;gzerror(input,&code);if(code!=Z_OK&&code!=Z_STREAM_END)ok=false;
  if(gzclose(input)!=Z_OK)ok=false;
  if(!ok)return false;
 }else{
  std::ifstream input(path);if(!input)return false;
  while(std::getline(input,line))if(!parse_line(line))return false;
  if(!input.eof())return false;
 }
 if(count==0)return false;
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
 std::size_t codepoint_count=0;
 for(std::size_t offset=0;offset<reading.size();offset+=utf8_width(static_cast<unsigned char>(reading[offset])))++codepoint_count;
 if (codepoint_count==0) return out;
 if(codepoint_count>256){
  if(resource_limit_exceeded)*resource_limit_exceeded=true;
  return out;
 }
 std::size_t start_byte=0;
 for (std::size_t start = 0; start < codepoint_count; ++start) {
  if(out.size()>=max_total_edges){
   if(resource_limit_exceeded)*resource_limit_exceeded=true;
   break;
  }
  LatticePositionTrace position_trace;
  position_trace.start = start;
  position_trace.fallback_enabled = fallback;
  std::vector<LatticeEdge> available_edges;std::vector<std::size_t> raw_counts;std::size_t node=0;
  std::size_t current_byte=start_byte;
  if(max_matches)for(std::size_t end=start;end<codepoint_count;++end){
   const auto cp=utf8_codepoint_at(reading,current_byte);current_byte+=utf8_width(static_cast<unsigned char>(reading[current_byte]));const auto& transitions=nodes_[node].next;auto it=std::lower_bound(transitions.begin(),transitions.end(),cp,[](const auto& edge,std::uint32_t value){return edge.first<value;});if(it==transitions.end()||it->first!=cp)break;node=it->second;
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
  if(fallback&&position_trace.available_dictionary_match_count==0&&max_matches){const auto width=utf8_width(static_cast<unsigned char>(reading[start_byte]));const auto surface=reading.substr(start_byte,width);out.push_back(LatticeEdge{start,start+1,{LatticeCandidate{"kana:"+surface,surface,10000,0,0,0,{},{},{}}}});position_trace.fallback_generated=true;}
  else if(fallback&&position_trace.available_dictionary_match_count)position_trace.fallback_suppressed_by_dictionary=true;
  if (trace) trace->push_back(std::move(position_trace));
  start_byte+=utf8_width(static_cast<unsigned char>(reading[start_byte]));
 }
 return out;
}

}
