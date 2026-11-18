#include "kana_decoder/engine.hpp"
#include <algorithm>
#include <cstdint>
namespace kana_decoder {
std::vector<DecodedPath> MorphologyDecoder::decode(std::size_t length,
 const std::vector<LatticeEdge>& lattice,const MaskedNgramScorer& scorer,
 const std::vector<std::string>& right_context,const DecoderOptions& opt,const ConnectionModel* connections,
 std::vector<CandidateScoreTrace>* trace) const {
 if(trace) trace->clear();
 if(length==0||opt.beam_width==0||opt.max_edges_per_position==0||
    opt.max_candidates_per_edge==0||opt.max_paths==0)return {};
 // Hard ceilings keep caller-supplied options from defeating bounded-work guarantees.
 const std::size_t beam_limit=std::min<std::size_t>(opt.beam_width,8);
 const std::size_t edge_limit=std::min<std::size_t>(opt.max_edges_per_position,16);
 const std::size_t candidate_limit=std::min<std::size_t>(opt.max_candidates_per_edge,8);
 const std::size_t path_limit=std::min<std::size_t>(opt.max_paths,8);
 const std::size_t expansion_limit=opt.max_expanded_states;
 const std::size_t trace_limit=std::min<std::size_t>(opt.max_trace_records,4096);
 if(expansion_limit==0)return {};
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
   auto candidates=edge->candidates;
   std::stable_sort(candidates.begin(),candidates.end(),[](const LatticeCandidate&a,const LatticeCandidate&b){
    return static_cast<std::int64_t>(a.word_cost)+a.connection_cost<
           static_cast<std::int64_t>(b.word_cost)+b.connection_cost;
   });
   if(candidates.size()>candidate_limit)candidates.resize(candidate_limit);
   for(const auto& state:beams[pos])for(const auto& candidate:candidates){
    if(candidate.id.empty()||candidate.surface.empty())continue;
    if(expanded_states>=expansion_limit)return {};
    ++expanded_states;
    State next=state;
    const std::int32_t transition=(connections&&state.has_last)?connections->cost(state.last_right_id,candidate.left_id):0;
    const double morph=-static_cast<double>(candidate.word_cost)-
                       static_cast<double>(candidate.connection_cost)-static_cast<double>(transition);
    const double raw_context=scorer.score(candidate.id,state.history,right_context);
    const double ctx=raw_context*opt.context_weight;
    const double local=morph+ctx;
    if(trace && trace->size()<trace_limit) trace->push_back(CandidateScoreTrace{
      edge->start,edge->end,candidate.id,candidate.surface,
      candidate.word_cost,candidate.connection_cost,transition,
      raw_context,opt.context_weight,morph,ctx,local,
      candidate.id.rfind("kana:",0)==0});
    next.path.score+=local;
    next.path.tokens.push_back(DecodedToken{edge->start,edge->end,
      Candidate{candidate.id,candidate.surface,candidate.pos,candidate.inflection_type,candidate.inflection_form},morph,ctx});
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

} // namespace kana_decoder
