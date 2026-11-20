#include "kana_decoder/japanese_decoder.hpp"
#include <algorithm>
#include <charconv>
#include <fstream>
#include <limits>
#include <stdexcept>
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
void LexiconTrie::compact_edges(){
 if(edges_compacted_)return;
 std::size_t total=0;for(const auto& node:nodes_)total+=node.next.size();
 edge_arena_.clear();edge_arena_.reserve(total);
 for(auto& node:nodes_){node.edge_offset=edge_arena_.size();node.edge_count=node.next.size();edge_arena_.insert(edge_arena_.end(),node.next.begin(),node.next.end());std::vector<std::pair<std::uint32_t,std::size_t>>().swap(node.next);}
 edges_compacted_=true;
}
void LexiconTrie::expand_edges(){
 if(!edges_compacted_)return;
 for(auto& node:nodes_){node.next.assign(edge_arena_.begin()+static_cast<std::ptrdiff_t>(node.edge_offset),edge_arena_.begin()+static_cast<std::ptrdiff_t>(node.edge_offset+node.edge_count));node.edge_offset=0;node.edge_count=0;}
 std::vector<std::pair<std::uint32_t,std::size_t>>().swap(edge_arena_);edges_compacted_=false;
}
void LexiconTrie::compact_candidates(){
 if(candidates_compacted_)return;
 std::size_t total=0;
 for(const auto& node:nodes_){
  if(node.values.size()>std::numeric_limits<std::size_t>::max()-total)throw std::length_error("candidate arena size overflow");
  total+=node.values.size();
 }
 candidate_arena_.clear();candidate_arena_.reserve(total);
 for(auto& node:nodes_){
  node.candidate_offset=candidate_arena_.size();node.candidate_count=node.values.size();
  candidate_arena_.insert(candidate_arena_.end(),node.values.begin(),node.values.end());
  std::vector<CompactCandidate>().swap(node.values);
 }
 candidates_compacted_=true;
}
void LexiconTrie::expand_candidates(){
 if(!candidates_compacted_)return;
 for(auto& node:nodes_){
  const auto begin=candidate_arena_.begin()+static_cast<std::ptrdiff_t>(node.candidate_offset);
  node.values.assign(begin,begin+static_cast<std::ptrdiff_t>(node.candidate_count));
  node.candidate_offset=0;node.candidate_count=0;
 }
 std::vector<CompactCandidate>().swap(candidate_arena_);candidates_compacted_=false;
}
LexiconTrie::CompactString LexiconTrie::store_candidate_string(const std::string& value){
 if(candidate_string_pool_.size()>std::numeric_limits<std::uint32_t>::max()||value.size()>std::numeric_limits<std::uint32_t>::max()-candidate_string_pool_.size())throw std::length_error("candidate string pool exceeds 32-bit offsets");
 CompactString ref{static_cast<std::uint32_t>(candidate_string_pool_.size()),static_cast<std::uint32_t>(value.size())};
 candidate_string_pool_.append(value);return ref;
}
std::string_view LexiconTrie::candidate_string(CompactString ref) const{
 if(static_cast<std::size_t>(ref.offset)+ref.size>candidate_string_pool_.size())return {};
 return std::string_view(candidate_string_pool_.data()+ref.offset,ref.size);
}
LexiconTrie::CompactString LexiconTrie::store_metadata_string(const std::string& value){
 if(value.empty())return {};
 const auto found=interned_metadata_strings_.find(value);
 if(found!=interned_metadata_strings_.end())return found->second;
 const auto ref=store_candidate_string(value);
 interned_metadata_strings_.emplace(value,ref);
 return ref;
}
LatticeCandidate LexiconTrie::materialize_candidate(const CompactCandidate& value) const{
 LatticeCandidate out;out.id=candidate_string(value.id);out.surface=candidate_string(value.surface);out.pos=candidate_string(value.pos);
 out.inflection_type=candidate_string(value.inflection_type);out.inflection_form=candidate_string(value.inflection_form);
 out.word_cost=value.word_cost;out.connection_cost=value.connection_cost;out.left_id=value.left_id;out.right_id=value.right_id;return out;
}
bool LexiconTrie::add(const std::string& reading,const LatticeCandidate& candidate){
 if(edges_compacted_)expand_edges();
 if(candidates_compacted_)expand_candidates();
 if(reading.empty()||!valid_utf8_impl(reading)||!valid_utf8_impl(candidate.id)||!valid_utf8_impl(candidate.surface)||!valid_utf8_impl(candidate.pos)||!valid_utf8_impl(candidate.inflection_type)||!valid_utf8_impl(candidate.inflection_form)||candidate.id.empty()||candidate.surface.empty())return false;
 std::size_t node=0;
 for(std::size_t offset=0;offset<reading.size();offset+=utf8_width(static_cast<unsigned char>(reading[offset]))){const auto cp=utf8_codepoint_at(reading,offset);auto& transitions=nodes_[node].next;auto it=std::lower_bound(transitions.begin(),transitions.end(),cp,[](const auto& edge,std::uint32_t value){return edge.first<value;});if(it==transitions.end()||it->first!=cp){
  std::size_t next=nodes_.size();transitions.insert(it,{cp,next});nodes_.push_back(Node{});node=next;
 }else node=it->second;}
 auto& values=nodes_[node].values;
 auto duplicate=std::find_if(values.begin(),values.end(),[&](const CompactCandidate&v){return candidate_string(v.id)==candidate.id&&candidate_string(v.surface)==candidate.surface;});
 if(duplicate!=values.end())return false;
 CompactCandidate compact;compact.id=store_candidate_string(candidate.id);compact.surface=store_candidate_string(candidate.surface);compact.pos=store_metadata_string(candidate.pos);compact.inflection_type=store_metadata_string(candidate.inflection_type);compact.inflection_form=store_metadata_string(candidate.inflection_form);
 compact.word_cost=candidate.word_cost;compact.connection_cost=candidate.connection_cost;compact.left_id=candidate.left_id;compact.right_id=candidate.right_id;
 values.push_back(compact);++entry_count_;return true;
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
 parsed.compact_edges();parsed.compact_candidates();*this=std::move(parsed);if(loaded)*loaded=count;return true;
}
void LexiconTrie::build_candidate_pos_index() {
 candidate_pos_index_.clear();
 for(const auto& node:nodes_) {
  const auto begin=candidates_compacted_?
   candidate_arena_.begin()+static_cast<std::ptrdiff_t>(node.candidate_offset):node.values.begin();
  const auto end=begin+static_cast<std::ptrdiff_t>(candidates_compacted_?node.candidate_count:node.values.size());
  for(auto it=begin;it!=end;++it) {
   if(it->id.size==0||it->pos.size==0) continue;
   candidate_pos_index_.push_back(PosIndexEntry{it->id,it->pos});
  }
 }
 // Sort the sole backing buffer to avoid retaining a temporary entries vector.
 std::sort(candidate_pos_index_.begin(),candidate_pos_index_.end(),[this](const PosIndexEntry& a,const PosIndexEntry& b) {
  return candidate_string(a.id)<candidate_string(b.id);
 });
 // Fold duplicate IDs in place; differing POS values resolve to empty.
 std::size_t write=0;
 for(std::size_t read=0;read<candidate_pos_index_.size();) {
  const auto id=candidate_pos_index_[read].id;
  CompactString pos=candidate_pos_index_[read].pos;
  std::size_t next=read+1;
  while(next<candidate_pos_index_.size()&&candidate_string(candidate_pos_index_[next].id)==candidate_string(id)) {
   if(candidate_string(candidate_pos_index_[next].pos)!=candidate_string(pos)) pos=CompactString{};
   ++next;
  }
  candidate_pos_index_[write++]=PosIndexEntry{id,pos};
  read=next;
 }
 candidate_pos_index_.resize(write);
 // Release the excess capacity retained from pre-folding candidate IDs.
 // Allocate only the compacted size, then swap so the old oversized buffer
 // is released when the temporary goes out of scope.
 if(write<candidate_pos_index_.capacity()) {
  std::vector<PosIndexEntry> compact;
  compact.reserve(write);
  compact.insert(compact.end(),candidate_pos_index_.begin(),candidate_pos_index_.end());
  candidate_pos_index_.swap(compact);
 }
}
std::string LexiconTrie::candidate_pos(const std::string& candidate_id) const {
 const auto found=std::lower_bound(candidate_pos_index_.begin(),candidate_pos_index_.end(),candidate_id,
  [this](const PosIndexEntry& entry,const std::string& id) {
   return candidate_string(entry.id).compare(id)<0;
  });
 if(found==candidate_pos_index_.end()||candidate_string(found->id)!=candidate_id) return {};
 return std::string(candidate_string(found->pos));
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
   const auto cp=utf8_codepoint_at(reading,current_byte);current_byte+=utf8_width(static_cast<unsigned char>(reading[current_byte]));const auto& current=nodes_[node];const auto edge_begin=edges_compacted_?edge_arena_.begin()+static_cast<std::ptrdiff_t>(current.edge_offset):current.next.begin();const auto edge_end=edges_compacted_?edge_begin+static_cast<std::ptrdiff_t>(current.edge_count):current.next.end();auto it=std::lower_bound(edge_begin,edge_end,cp,[](const auto& edge,std::uint32_t value){return edge.first<value;});if(it==edge_end||it->first!=cp)break;node=it->second;
   const auto& terminal=nodes_[node];
   const auto candidate_begin=candidates_compacted_?
    candidate_arena_.begin()+static_cast<std::ptrdiff_t>(terminal.candidate_offset):terminal.values.begin();
   const auto candidate_count=candidates_compacted_?terminal.candidate_count:terminal.values.size();
   if(candidate_count){
    position_trace.available_candidate_count+=candidate_count;
    std::vector<LatticeCandidate> materialized;materialized.reserve(candidate_count);
    for(auto it=candidate_begin;it!=candidate_begin+static_cast<std::ptrdiff_t>(candidate_count);++it)materialized.push_back(materialize_candidate(*it));
    available_edges.push_back(LatticeEdge{start,end+1,{}});raw_counts.push_back(candidate_count);
    available_edges.back().candidates=retain_surface_diverse(materialized,std::min<std::size_t>(8,materialized.size()));
   }
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
