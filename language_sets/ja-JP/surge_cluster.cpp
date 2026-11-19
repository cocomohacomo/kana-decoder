#include "kana_decoder/engine.hpp"
#include <algorithm>
#include <utility>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>

namespace kana_decoder {
namespace {
bool split_fields(const std::string& line, std::vector<std::string>& fields) {
 fields.clear();
 std::size_t begin=0;
 for (;;) {
  const auto end=line.find('\t',begin);
  fields.push_back(line.substr(begin,end==std::string::npos?std::string::npos:end-begin));
  if(end==std::string::npos) break;
  begin=end+1;
 }
 return true;
}
bool parse_u32(const std::string& text,std::uint32_t& value) {
 if(text.empty()) return false;
 std::uint64_t parsed=0;
 for(char ch:text) {
  if(ch<'0'||ch>'9') return false;
  const auto digit=static_cast<unsigned>(ch-'0');
  if(parsed>(std::numeric_limits<std::uint32_t>::max()-digit)/10U) return false;
  parsed=parsed*10U+digit;
 }
 value=static_cast<std::uint32_t>(parsed);
 return true;
}
bool valid_id(const std::string& value) {
 return !value.empty()&&value.size()<=128&&
  value.find_first_of("\t\r\n") == std::string::npos;
}
}
std::string SurgeClusterModel::evidence_key(std::uint32_t cluster,std::uint32_t distance,const std::string& candidate) {
 return std::to_string(cluster)+"\x1f"+std::to_string(distance)+"\x1f"+candidate;
}
std::string SurgeClusterModel::total_key(std::uint32_t cluster,std::uint32_t distance) {
 return std::to_string(cluster)+"\x1f"+std::to_string(distance);
}
bool SurgeClusterModel::load_tsv(const std::string& path) {
 std::ifstream input(path,std::ios::binary);
 if(!input) return false;
 input.seekg(0,std::ios::end);
 const auto bytes=input.tellg();
 if(bytes<0||bytes>static_cast<std::streamoff>(64U*1024U*1024U)) return false;
 input.seekg(0,std::ios::beg);
 SurgeClusterModel parsed;
 std::string line;
 if(!std::getline(input,line)||line!="KANA_SURGE_CLUSTER\t1") return false;
 std::map<std::string,std::string> metadata;
 std::set<std::uint32_t> clusters;
 std::size_t memberships=0,evidence_records=0,total_records=0;
 while(std::getline(input,line)) {
  if(line.empty()||line.size()>4096||line.find('\r')!=std::string::npos) return false;
  std::vector<std::string> f;
  split_fields(line,f);
  if(f[0]=="META") {
   if(f.size()!=3||f[1].empty()||metadata.count(f[1])) return false;
   metadata.emplace(f[1],f[2]);
  } else if(f[0]=="TOKEN") {
   std::uint32_t cluster=0;
   if(f.size()!=3||!parse_u32(f[1],cluster)||cluster>=4096||!valid_id(f[2])) return false;
   if(!parsed.token_cluster_.emplace(f[2],cluster).second) return false;
   clusters.insert(cluster);
   if(++memberships>100000) return false;
  } else if(f[0]=="EVIDENCE") {
   std::uint32_t cluster=0,distance=0,count=0;
   if(f.size()!=5||!parse_u32(f[1],cluster)||cluster>=4096||
      !parse_u32(f[2],distance)||(distance!=1&&distance!=2)||
      !valid_id(f[3])||!parse_u32(f[4],count)||count==0) return false;
   if(!parsed.evidence_.emplace(evidence_key(cluster,distance,f[3]),count).second) return false;
   clusters.insert(cluster);
   if(++evidence_records>500000) return false;
  } else if(f[0]=="TOTAL") {
   std::uint32_t cluster=0,distance=0,count=0;
   if(f.size()!=4||!parse_u32(f[1],cluster)||cluster>=4096||
      !parse_u32(f[2],distance)||(distance!=1&&distance!=2)||
      !parse_u32(f[3],count)||count==0) return false;
   if(!parsed.totals_.emplace(total_key(cluster,distance),count).second) return false;
   clusters.insert(cluster);
   if(++total_records>8192) return false;
  } else return false;
 }
 if(!input.eof()||metadata.size()!=8||memberships==0) return false;
 const char* required[]={"train_sequences","vocabulary_considered","cluster_count",
  "clustered_tokens","window","max_history","gap_threshold","smoothing_alpha"};
 for(const char* key:required) if(!metadata.count(key)) return false;
 std::uint32_t train=0,vocab=0,cluster_count=0,clustered=0,window=0,history=0;
 if(!parse_u32(metadata["train_sequences"],train)||train==0||
    !parse_u32(metadata["vocabulary_considered"],vocab)||vocab==0||
    !parse_u32(metadata["cluster_count"],cluster_count)||cluster_count==0||
    !parse_u32(metadata["clustered_tokens"],clustered)||clustered!=memberships||
    !parse_u32(metadata["window"],window)||window!=2||
    !parse_u32(metadata["max_history"],history)||history!=2||
    clusters.size()!=cluster_count) return false;
 for(const auto cluster:clusters) if(cluster>=cluster_count) return false;
 char* end=nullptr;
 const double threshold=std::strtod(metadata["gap_threshold"].c_str(),&end);
 if(end==metadata["gap_threshold"].c_str()||*end!='\0'||!std::isfinite(threshold)||
    threshold<0.0||threshold>1000000.0) return false;
 std::map<std::string,std::uint64_t> evidence_sums;
 for(const auto& item:parsed.evidence_) {
  const auto sep=item.first.find('\x1f');
  const auto second=item.first.find('\x1f',sep+1);
  const auto key=item.first.substr(0,second);
  if(!parsed.totals_.count(key)) return false;
  evidence_sums[key]+=item.second;
 }
 for(const auto& item:parsed.totals_) {
  const auto sum=evidence_sums.find(item.first);
  if(sum==evidence_sums.end()||sum->second!=item.second) return false;
 }
 if(evidence_sums.size()!=parsed.totals_.size()) return false;
 for(const auto& item:parsed.token_cluster_) {
  if(!clusters.count(item.second)) return false;
 }
 char* alpha_end=nullptr;
 const double alpha=std::strtod(metadata["smoothing_alpha"].c_str(),&alpha_end);
 if(alpha_end==metadata["smoothing_alpha"].c_str()||*alpha_end!='\0'||
    !std::isfinite(alpha)||alpha<=0.0||alpha>100.0) return false;
 parsed.gap_threshold_=threshold;
 parsed.smoothing_alpha_=alpha;
 parsed.vocab_size_=std::max<std::size_t>(1,memberships*4U);
 *this=std::move(parsed);
 return true;
}
double SurgeClusterModel::evidence_score(const std::string& candidate,
 const std::vector<std::string>& history,bool* has_evidence) const {
 if(has_evidence) *has_evidence=false;
 if(candidate.empty()||history.empty()) return 0.0;
 double sum=0.0;
 std::size_t observations=0;
 const double alpha=smoothing_alpha_;
 const double vocab=static_cast<double>(vocab_size_);
 for(std::size_t distance=1;distance<=2&&distance<=history.size();++distance) {
  const auto found_cluster=token_cluster_.find(history[history.size()-distance]);
  if(found_cluster==token_cluster_.end()) continue;
  const auto key=total_key(found_cluster->second,static_cast<std::uint32_t>(distance));
  const auto total=totals_.find(key);
  if(total==totals_.end()) continue;
  const auto evidence=evidence_.find(evidence_key(found_cluster->second,
    static_cast<std::uint32_t>(distance),candidate));
  if(evidence==evidence_.end()) continue;
  sum+=std::log((static_cast<double>(evidence->second)+alpha)/
    (static_cast<double>(total->second)+alpha*vocab));
  ++observations;
 }
 if(observations==0) return 0.0;
 if(has_evidence) *has_evidence=true;
 return sum/static_cast<double>(observations);
}
} // namespace kana_decoder
