#include "kana_decoder/engine.hpp"
#include <algorithm>
#include <cstdint>
namespace kana_decoder {
std::vector<DecodedPath> MorphologyDecoder::decode(std::size_t length,
 const std::vector<LatticeEdge>& lattice,const MaskedNgramScorer& scorer,
 const std::vector<std::string>& right_context,const DecoderOptions& opt,const ConnectionModel* connections,
 std::vector<CandidateScoreTrace>* trace,bool* resource_limit_exceeded) const {
 if(trace) trace->clear();
 if(resource_limit_exceeded)*resource_limit_exceeded=false;
 if(length==0)return {};
 if(length>256||opt.beam_width==0||opt.max_edges_per_position==0||
    opt.max_candidates_per_edge==0||opt.max_paths==0){
  if(resource_limit_exceeded)*resource_limit_exceeded=true;
  return {};
 }
 if(lattice.size()>4096){
  if(resource_limit_exceeded)*resource_limit_exceeded=true;
  return {};
 }
 std::size_t total_candidates=0;
 for(const auto& edge:lattice){
  if(edge.candidates.size()>32768-total_candidates){
   if(resource_limit_exceeded)*resource_limit_exceeded=true;
   return {};
  }
  total_candidates+=edge.candidates.size();
 }
 // Hard ceilings keep caller-supplied options from defeating bounded-work guarantees.
 const std::size_t beam_limit=std::min<std::size_t>(opt.beam_width,8);
 const std::size_t edge_limit=std::min<std::size_t>(opt.max_edges_per_position,16);
 const std::size_t candidate_limit=std::min<std::size_t>(opt.max_candidates_per_edge,8);
 const std::size_t path_limit=std::min<std::size_t>(opt.max_paths,8);
 const std::size_t expansion_limit=std::min<std::size_t>(opt.max_expanded_states,262144);
 const std::size_t trace_limit=std::min<std::size_t>(opt.max_trace_records,4096);
 if(expansion_limit==0){if(resource_limit_exceeded)*resource_limit_exceeded=true;return {};}
 std::size_t expanded_states=0;
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
   std::vector<const LatticeCandidate*> candidates;
   candidates.reserve(std::min(candidate_limit,edge->candidates.size()));
   auto less_cost=[](const LatticeCandidate* a,const LatticeCandidate* b){
    return static_cast<std::int64_t>(a->word_cost)+a->connection_cost<
           static_cast<std::int64_t>(b->word_cost)+b->connection_cost;
   };
   for(const auto& candidate:edge->candidates){
    const auto* candidate_ptr=&candidate;
    auto where=std::find_if(candidates.begin(),candidates.end(),
     [&](const LatticeCandidate* current){return less_cost(candidate_ptr,current);});
    if(candidates.size()<candidate_limit)candidates.insert(where,candidate_ptr);
    else if(where!=candidates.end()){
     candidates.insert(where,candidate_ptr);
     candidates.pop_back();
    }
   }
   for(const auto& state:beams[pos]){
    bool surge_resolved=false;
    if(opt.surge_left_context_gate&&!state.history.empty()){
     std::vector<std::pair<std::string,double>> surface_evidence;
     for(const auto* candidate_ptr:candidates){
      const auto& candidate=*candidate_ptr;
      if(!scorer.has_left_context_evidence(candidate.id,state.history))continue;
      const double evidence=scorer.contextual_score(candidate.id,state.history,{});
      auto found=std::find_if(surface_evidence.begin(),surface_evidence.end(),
       [&](const auto& item){return item.first==candidate.surface;});
      if(found==surface_evidence.end())surface_evidence.emplace_back(candidate.surface,evidence);
      else if(evidence>found->second)found->second=evidence;
     }
     std::stable_sort(surface_evidence.begin(),surface_evidence.end(),
      [](const auto& a,const auto& b){return a.second>b.second;});
     surge_resolved=surface_evidence.size()>=2&&
      surface_evidence[0].second-surface_evidence[1].second>=opt.surge_context_margin;
    }
    for(const auto* candidate_ptr:candidates){
    const auto& candidate=*candidate_ptr;
    if(candidate.id.empty()||candidate.surface.empty())continue;
    if(expanded_states>=expansion_limit){
     if(resource_limit_exceeded)*resource_limit_exceeded=true;
     return {};
    }
    ++expanded_states;
    State next=state;
    const std::int32_t transition=(connections&&state.has_last)?connections->cost(state.last_right_id,candidate.left_id):0;
    const double morph=-static_cast<double>(candidate.word_cost)-
                       static_cast<double>(candidate.connection_cost)-static_cast<double>(transition);
    const double context_evidence=opt.surge_left_context_gate
      ? (surge_resolved?scorer.contextual_score(candidate.id,state.history,{}):0.0)
      : scorer.contextual_score(candidate.id,state.history,right_context);
    const double raw_context=opt.surge_left_context_gate
      ? context_evidence
      : scorer.score(candidate.id,state.history,right_context);
    const double ctx=raw_context*opt.context_weight;
    const double local=morph+ctx;
    if(trace && trace->size()<trace_limit) trace->push_back(CandidateScoreTrace{
      edge->start,edge->end,candidate.id,candidate.surface,
      candidate.word_cost,candidate.connection_cost,transition,
      raw_context,context_evidence,opt.context_weight,morph,ctx,local,
      candidate.id.rfind("kana:",0)==0});
    next.path.score+=local;
    next.path.tokens.push_back(DecodedToken{edge->start,edge->end,
      Candidate{candidate.id,candidate.surface,candidate.pos,candidate.inflection_type,candidate.inflection_form},morph,ctx});
    next.history.push_back(candidate.id);next.last_right_id=candidate.right_id;next.has_last=true;
    if(next.history.size()>2)next.history.erase(next.history.begin());
    beams[edge->end].push_back(std::move(next));
    }
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

} // namespace kana_decoder
