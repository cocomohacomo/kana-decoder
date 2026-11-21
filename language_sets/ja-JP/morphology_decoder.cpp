#include "kana_decoder/japanese_decoder.hpp"
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>
namespace {
std::int64_t candidate_cost(const kana_decoder::LatticeCandidate& c){return static_cast<std::int64_t>(c.word_cost)+c.connection_cost;}
std::vector<const kana_decoder::LatticeCandidate*> retain_surface_diverse(const std::vector<kana_decoder::LatticeCandidate>& source,std::size_t limit){
 std::vector<const kana_decoder::LatticeCandidate*> selected;selected.reserve(std::min(limit,source.size()));
 auto insert_ordered=[&](const kana_decoder::LatticeCandidate* candidate){
  const auto cost=candidate_cost(*candidate);
  const auto where=std::upper_bound(selected.begin(),selected.end(),cost,
   [](std::int64_t value,const auto* item){return value<candidate_cost(*item);});
  selected.insert(where,candidate);
 };
 for(const auto& candidate:source){
  const auto* ptr=&candidate;
  auto same=std::find_if(selected.begin(),selected.end(),[&](const auto* item){return item->surface==candidate.surface;});
  if(same!=selected.end()){
   if(candidate_cost(candidate)<candidate_cost(**same){
    selected.erase(same);
    insert_ordered(ptr);
   }
   continue;
  }
  if(selected.size()<limit)insert_ordered(ptr);
  else if(limit&&candidate_cost(candidate)<candidate_cost(*selected.back())){
   selected.pop_back();
   insert_ordered(ptr);
  }
 }
 if(selected.size()<limit)for(const auto& candidate:source){
  if(selected.size()>=limit)break;
  const auto* ptr=&candidate;
  if(std::find(selected.begin(),selected.end(),ptr)!=selected.end())continue;
  insert_ordered(ptr);
 }
 return selected;
}
std::string rendered_surface(const kana_decoder::DecodedPath& path){
 std::string surface;
 for(const auto& token:path.tokens)surface+=token.candidate.surface;
 return surface;
}
template<class Range>
void append_identity_range(std::string& key,const Range& values){
 for(const auto& value:values){key+=std::to_string(value.size());key.push_back(':');key+=value;key.push_back('|');}
}
template<class State>
std::string beam_context_identity(const State& state){
 std::string key=rendered_surface(state.path);
 key.push_back('|');
 key+=std::to_string(state.has_last?state.last_right_id:0);
 key.push_back('|');
 append_identity_range(key,state.history);
 key.push_back('|');
 append_identity_range(key,state.pos_history);
 return key;
}
std::vector<const kana_decoder::LatticeEdge*> retain_span_diverse(std::vector<const kana_decoder::LatticeEdge*> edges,std::size_t limit){
 std::stable_sort(edges.begin(),edges.end(),[](const auto* a,const auto* b){return a->end<b->end;});if(edges.size()<=limit)return edges;
 std::vector<const kana_decoder::LatticeEdge*> out;out.reserve(limit);if(limit==1){out.push_back(edges.back());return out;}for(std::size_t i=0;i<limit;++i)out.push_back(edges[i*(edges.size()-1)/(limit-1)]);return out;
}
}
namespace kana_decoder {
std::vector<DecodedPath> MorphologyDecoder::decode(std::size_t length,
 const std::vector<LatticeEdge>& lattice,const MaskedNgramScorer& scorer,
 const std::vector<std::string>& right_context,const DecoderOptions& opt,const ConnectionModel* connections,
 std::vector<CandidateScoreTrace>* trace,bool* resource_limit_exceeded) const {
 if(trace) trace->clear();
 if(resource_limit_exceeded)*resource_limit_exceeded=false;
 if(length==0)return {};
 if(!std::isfinite(opt.context_weight)||opt.context_weight<0.0||
    !std::isfinite(opt.surge_context_margin)||opt.surge_context_margin<0.0||
    !std::isfinite(opt.surge_cluster_weight)||opt.surge_cluster_weight<0.0||
    !std::isfinite(opt.pos_context_weight)||opt.pos_context_weight<0.0){
  if(resource_limit_exceeded)*resource_limit_exceeded=true;
  return {};
 }
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
 struct State { DecodedPath path; std::vector<std::string> history; std::vector<std::string> pos_history; std::int32_t last_right_id=0; bool has_last=false; std::size_t trace_index=std::numeric_limits<std::size_t>::max(); };
 std::vector<std::vector<State>> beams(length+1);
 std::vector<std::vector<std::size_t>> trace_indices_by_end(length+1);
 beams[0].push_back(State{});
 auto prune=[&](std::vector<State>& states,bool continuation_needed,std::size_t position){
  if(trace){for(const auto index:trace_indices_by_end[position])if(index<trace->size())(*trace)[index].beam_survived=false;}
  std::stable_sort(states.begin(),states.end(),[](const State&a,const State&b){return a.path.score>b.path.score;});
  auto mark_survivors=[&](){if(trace){for(const auto& state:states)if(state.trace_index<trace->size())(*trace)[state.trace_index].beam_survived=true;}};
  if(states.size()<=beam_limit){mark_survivors();return;}
  if(!opt.preserve_rendered_surface_diversity){
   states.resize(beam_limit);
   mark_survivors();
   return;
  }
  // Preserve distinct rendered outputs at the final position. At intermediate
  // positions, distinguish continuation contexts so future transitions retain
  // their state. This avoids sacrificing final surface diversity to redundant
  // internal histories.
  std::vector<std::size_t> retained_indices;
  retained_indices.reserve(beam_limit);
  std::unordered_set<std::string> seen_contexts;
  seen_contexts.reserve(beam_limit);
  std::vector<bool> retained(states.size(),false);
  for(std::size_t i=0;i<states.size()&&retained_indices.size()<beam_limit;++i){
   const auto identity=continuation_needed?beam_context_identity(states[i]):rendered_surface(states[i].path);
   if(seen_contexts.insert(identity).second){
    retained_indices.push_back(i);
    retained[i]=true;
   }
  }
  for(std::size_t i=0;i<states.size()&&retained_indices.size()<beam_limit;++i){
   if(!retained[i])retained_indices.push_back(i);
  }
  std::vector<State> diverse;
  diverse.reserve(beam_limit);
  for(const auto index:retained_indices)diverse.push_back(std::move(states[index]));
  states.swap(diverse);
  mark_survivors();
 };
 for(std::size_t pos=0;pos<length;++pos){
  if(beams[pos].empty())continue;
  auto edges=retain_span_diverse(at[pos],edge_limit);
  for(const auto* edge:edges){
   std::vector<const LatticeCandidate*> candidates=retain_surface_diverse(edge->candidates,candidate_limit);
   const auto morphology_candidates=candidates;
   for(const auto& state:beams[pos]){
    candidates=morphology_candidates;
    bool cluster_resolved=false;
    std::string cluster_winner;
    double cluster_gap=0.0;
    double cluster_override_bonus=0.0;
    std::vector<std::pair<const LatticeCandidate*,double>> selected_supported_scores;
    if(opt.surge_cluster_model&&!state.history.empty()){
     struct SurfaceScore { std::string surface; double score; };
     std::vector<SurfaceScore> surface_scores;
     std::unordered_map<std::string,std::size_t> surface_score_indices;
     // Only a bounded prefix of supported candidates is needed: the expansion
     // starts with at most four morphology candidates and can add at most
     // candidate_limit more. Keep enough top evidence candidates to fill it
     // even when every seeded candidate is already present in the evidence list.
     std::vector<std::pair<const LatticeCandidate*,double>> supported_top;
     const std::size_t seed_count=std::min<std::size_t>(4,candidates.size());
     const std::size_t supported_limit=candidate_limit+seed_count;
     // Preserve scores for morphology seeds even when they fall below the
     // bounded evidence shortlist. Both stores are strictly size-limited.
     std::vector<std::pair<const LatticeCandidate*,double>> seed_supported_scores;
     seed_supported_scores.reserve(seed_count);
     supported_top.reserve(supported_limit);
     surface_score_indices.reserve(std::min<std::size_t>(edge->candidates.size(),256));
     for(const auto& candidate:edge->candidates){
      bool has=false;
      const double score=opt.surge_cluster_model->evidence_score(candidate.id,state.history,&has);
      if(!has)continue;
      if(std::find(morphology_candidates.begin(),
                   morphology_candidates.begin()+static_cast<std::ptrdiff_t>(seed_count),
                   &candidate)!=morphology_candidates.begin()+static_cast<std::ptrdiff_t>(seed_count))
       seed_supported_scores.emplace_back(&candidate,score);
      const auto inserted=surface_score_indices.emplace(candidate.surface,surface_scores.size());
      if(inserted.second)surface_scores.push_back({candidate.surface,score});
      else {
       auto& current=surface_scores[inserted.first->second].score;
       if(score>current)current=score;
      }
      if(supported_limit){
       const auto where=std::find_if(supported_top.begin(),supported_top.end(),
        [&](const auto& item){return score>item.second;});
       if(where!=supported_top.end()||supported_top.size()<supported_limit){
        supported_top.insert(where,{&candidate,score});
        if(supported_top.size()>supported_limit)supported_top.pop_back();
       }
      }
     }
     // A linear best-two scan avoids sorting every distinct surface. Strict
     // comparisons preserve the first-seen order for equal evidence scores.
     std::size_t best_index=surface_scores.size(),second_index=surface_scores.size();
     for(std::size_t i=0;i<surface_scores.size();++i){
      if(best_index==surface_scores.size()||
         surface_scores[i].score>surface_scores[best_index].score){
       second_index=best_index;best_index=i;
      }else if(second_index==surface_scores.size()||
               surface_scores[i].score>surface_scores[second_index].score){
       second_index=i;
      }
     }
     if(second_index<surface_scores.size()){
      cluster_gap=surface_scores[best_index].score-surface_scores[second_index].score;
      cluster_resolved=cluster_gap>=opt.surge_cluster_model->gap_threshold();
      if(cluster_resolved)cluster_winner=surface_scores[best_index].surface;
     }
     if(cluster_resolved&&opt.surge_cluster_weight>0.0){
      double best_morph=-std::numeric_limits<double>::infinity();
      double winner_morph=-std::numeric_limits<double>::infinity();
      for(const auto& candidate:edge->candidates){
       if(candidate.id.empty()||candidate.surface.empty())continue;
       const auto transition=(connections&&state.has_last)
        ? connections->cost(state.last_right_id,candidate.left_id):0;
       const double score=-static_cast<double>(candidate.word_cost)-
        static_cast<double>(candidate.connection_cost)-static_cast<double>(transition);
       best_morph=std::max(best_morph,score);
       if(candidate.surface==cluster_winner)winner_morph=std::max(winner_morph,score);
      }
      if(std::isfinite(best_morph)&&std::isfinite(winner_morph))
       cluster_override_bonus=(std::max(0.0,best_morph-winner_morph)+cluster_gap)*opt.surge_cluster_weight;
     }
     if(!supported_top.empty()){
      std::vector<const LatticeCandidate*> expanded;
      for(std::size_t i=0;i<seed_count;++i)expanded.push_back(candidates[i]);
      for(const auto& item:supported_top){
       if(expanded.size()>=candidate_limit)break;
       if(std::find(expanded.begin(),expanded.end(),item.first)==expanded.end())
        expanded.push_back(item.first);
      }
      candidates.swap(expanded);
     }
     // Reuse scores from the bounded seed/evidence stores. The supported
     // shortlist has enough capacity for every non-seed candidate needed to
     // fill the expansion, while the seed store covers any lower-ranked seeds.
     selected_supported_scores.reserve(candidates.size());
     for(const auto* candidate:candidates){
      const auto seed=std::find_if(seed_supported_scores.begin(),seed_supported_scores.end(),
       [&](const auto& item){return item.first==candidate;});
      if(seed!=seed_supported_scores.end()){
       selected_supported_scores.emplace_back(candidate,seed->second);
       continue;
      }
      const auto supported=std::find_if(supported_top.begin(),supported_top.end(),
       [&](const auto& item){return item.first==candidate;});
      if(supported!=supported_top.end())
       selected_supported_scores.emplace_back(candidate,supported->second);
     }
    }
    bool surge_resolved=false;
    std::string ngram_winner;
    double ngram_gap=0.0;
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
     if(surface_evidence.size()>=2){
      ngram_gap=surface_evidence[0].second-surface_evidence[1].second;
      surge_resolved=ngram_gap>=opt.surge_context_margin;
      if(surge_resolved)ngram_winner=surface_evidence[0].first;
     }
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
    const double lexical_score=-static_cast<double>(candidate.word_cost)-
                               static_cast<double>(candidate.connection_cost);
    const double transition_score=-static_cast<double>(transition);
    const double morph=lexical_score+transition_score;
    const double context_evidence=opt.surge_left_context_gate
      ? (surge_resolved&&candidate.surface==ngram_winner
          ? ngram_gap : 0.0)
      : scorer.contextual_score(candidate.id,state.history,right_context);
    const double raw_context=opt.surge_left_context_gate
      ? context_evidence
      : scorer.score(candidate.id,state.history,right_context);
    const double ngram_context=raw_context*opt.context_weight;
    const double cluster_context=(cluster_resolved&&candidate.surface==cluster_winner)
      ? cluster_override_bonus : 0.0;
    const auto cluster_evidence = std::find_if(selected_supported_scores.begin(),selected_supported_scores.end(),
     [&](const auto& item){return item.first==candidate_ptr;});
    const bool has_cluster_evidence = cluster_evidence != selected_supported_scores.end();
    const double cluster_raw_evidence = has_cluster_evidence ? cluster_evidence->second : 0.0;
    const bool has_ngram_evidence=scorer.has_left_context_evidence(candidate.id,state.history) ||
      scorer.has_right_context_evidence(candidate.id,right_context);
    const double pos_raw=(opt.pos_ngram_scorer&&!candidate.pos.empty())
      ? opt.pos_ngram_scorer->contextual_score(candidate.pos,state.pos_history,{}) : 0.0;
    const bool has_pos_evidence=opt.pos_ngram_scorer&&!candidate.pos.empty() &&
      opt.pos_ngram_scorer->has_left_context_evidence(candidate.pos,state.pos_history);
    const double pos_context=pos_raw*opt.pos_context_weight;
    const double ctx=ngram_context+cluster_context+pos_context;
    const double local=morph+ctx;
    if(trace && trace->size()<trace_limit){
     trace->push_back(CandidateScoreTrace{
      edge->start,edge->end,candidate.id,candidate.surface,
      candidate.word_cost,candidate.connection_cost,transition,
      raw_context,context_evidence,opt.context_weight,morph,ctx,local,
      candidate.id.rfind("kana:",0)==0,false});
     next.trace_index=trace->size()-1;
     trace_indices_by_end[edge->end].push_back(next.trace_index);
    }
    next.path.score+=local;
    DecodedToken decoded_token;
    decoded_token.start=edge->start;
    decoded_token.end=edge->end;
    decoded_token.candidate.id = candidate.id;
    decoded_token.candidate.surface = candidate.surface;
    decoded_token.candidate.morphology.language_tag = "ja-JP";
    decoded_token.candidate.morphology.part_of_speech = candidate.pos;
    decoded_token.candidate.morphology.inflection_class = candidate.inflection_type;
    decoded_token.candidate.morphology.inflection_form = candidate.inflection_form;
    decoded_token.morphology_score=morph;
    decoded_token.context_score=ctx;
    decoded_token.lexical_score=lexical_score;
    decoded_token.transition_score=transition_score;
    decoded_token.ngram_score=ngram_context;
    decoded_token.cluster_score=cluster_context;
    decoded_token.cluster_raw_evidence=cluster_raw_evidence;
    decoded_token.pos_score=pos_context;
    decoded_token.has_transition_evidence=connections&&state.has_last;
    decoded_token.has_ngram_evidence=has_ngram_evidence;
    decoded_token.has_cluster_evidence=has_cluster_evidence;
    decoded_token.has_pos_evidence=has_pos_evidence;
    next.path.tokens.push_back(std::move(decoded_token));
    next.history.push_back(candidate.id);next.last_right_id=candidate.right_id;next.has_last=true;
    if(next.history.size()>3)next.history.erase(next.history.begin());
    if(!candidate.pos.empty())next.pos_history.push_back(candidate.pos);
    if(next.pos_history.size()>3)next.pos_history.erase(next.pos_history.begin());
    beams[edge->end].push_back(std::move(next));
    }
   }
   prune(beams[edge->end],edge->end<length,edge->end);
  }
 }
 std::vector<DecodedPath> out;
 for(auto& state:beams[length])out.push_back(std::move(state.path));
 std::stable_sort(out.begin(),out.end(),[](const DecodedPath&a,const DecodedPath&b){return a.score>b.score;});
 if(out.size()>path_limit)out.resize(path_limit);
 return out;
}

} // namespace kana_decoder
